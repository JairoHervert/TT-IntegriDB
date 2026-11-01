// serve_command.cpp - comando "tt_prover serve"
//
// Ejecuta el modulo probador como servidor de red: carga una sola vez delta,
// D~ y la clave publica, y responde por HTTP las consultas que le envie el
// verificador (protocolo en net/include/tt/net/protocol.hpp). Se detiene con
// Ctrl+C o con SIGTERM (por ejemplo, 'docker stop').

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "commands.hpp"
#include "prover_state.hpp"
#include "tt/cli/cli.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/mysql/connection.hpp"
#include "tt/mysql/row_store.hpp"
#include "tt/net/prover_server.hpp"
#include "tt/net/protocol.hpp"

namespace tt::prover {

namespace {

namespace fs = std::filesystem;
using cli::Clock;
using cli::seconds_since;

/*
 * Opciones del comando serve, ya leidas de la linea de comandos.
 *
 * Campos:
 *   state    carpeta con delta.txt y authenticated_db.bin
 *   keys     carpeta con public_key.txt (C7 a C14)
 *   server   interfaz, puerto e hilos del servidor
 */
struct ServeOptions {
    fs::path state = "setup";
    fs::path keys = "keys";
    net::ServerOptions server;
};

/*
 * Lee un entero positivo de una opcion (solo digitos, a lo mas 6).
 *
 * Errores:
 *   Lanza cli::UsageError si no es un entero entre min y max.
 */
int parse_int_option(const std::string& name, const std::string& text, int min, int max) {
    if (text.empty() || text.size() > 6 || text.find_first_not_of("0123456789") != std::string::npos ||
        std::stoi(text) < min || std::stoi(text) > max) {
        throw cli::UsageError(name + " espera un entero entre " + std::to_string(min) + " y " + std::to_string(max) +
                              ", se recibio '" + text + "'");
    }
    return std::stoi(text);
}

/*
 * Lee las opciones del comando; serve no recibe consulta.
 *
 * Errores:
 *   Lanza cli::UsageError si una opcion es desconocida, le falta su valor o
 *   si se escribio una palabra suelta.
 */
ServeOptions parse_serve_options(const std::vector<std::string>& args) {
    ServeOptions opt;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--state") {
            opt.state = cli::option_value(args, i);
        } else if (arg == "--keys") {
            opt.keys = cli::option_value(args, i);
        } else if (arg == "--host") {
            opt.server.host = cli::option_value(args, i);
        } else if (arg == "--port") {
            opt.server.port = parse_int_option("--port", cli::option_value(args, i), 1, 65535);
        } else if (arg == "--threads") {
            opt.server.threads = static_cast<std::size_t>(parse_int_option("--threads", cli::option_value(args, i), 1, 64));
        } else {
            throw cli::UsageError(arg.rfind("--", 0) == 0 ? "opcion desconocida: " + arg
                                                          : "serve no recibe consulta; se encontro '" + arg + "'");
        }
    }
    return opt;
}

/*
 * Imprime una peticion atendida en la bitacora. Las consultas a /v1/health
 * que salen bien no se imprimen, porque Docker las hace cada pocos segundos.
 */
void log_request(const net::RequestRecord& r) {
    if (r.path == net::kHealthPath && r.status == 200) return;
    const bool query = r.method == "POST" && r.path == net::kQueryPath;
    const std::string what = query ? "\"" + r.query + "\"" : r.method + " " + r.path;
    std::printf("[prover] %s %s -> %d (%s) en %.2f s: %s\n", r.client.c_str(), what.c_str(), r.status,
                cli::size_text(r.bytes).c_str(), r.seconds, r.note.c_str());
    if (r.stale_rows > 0) {
        std::fprintf(stderr,
                     "[prover] AVISO: %zu fila(s) de la base ya no son las que se autenticaron en Setup; el\n"
                     "         verificador rechazara esa respuesta.\n",
                     r.stale_rows);
    }
}

}  // namespace

/*
 * Implementacion: el texto fijo del comando.
 */
std::string serve_usage() {
    return std::string(
        "  tt_prover serve [--state <carpeta>] [--keys <carpeta>] [--host <interfaz>]\n"
        "                  [--port <puerto>] [--threads <n>]\n"
        "      Ejecuta el probador como servidor de red. Carga una vez delta, D~ y la\n"
        "      clave publica, y responde las consultas del verificador por HTTP:\n"
        "        POST /v1/query   cuerpo: la consulta, por ejemplo 'C4 6 8'\n"
        "        GET  /v1/health  estado del probador\n"
        "      --state <carpeta>  donde estan delta.txt y authenticated_db.bin\n"
        "                         (por defecto: setup).\n"
        "      --keys <carpeta>   donde esta public_key.txt (por defecto: keys). Sin\n"
        "                         ella, C7 a C14 se responden con error.\n"
        "      --host <interfaz>  interfaz donde escucha (por defecto: 0.0.0.0, todas).\n"
        "      --port <puerto>    puerto TCP (por defecto: ") +
           std::to_string(net::kDefaultPort) +
           ").\n"
           "      --threads <n>      hilos para atender conexiones (por defecto: 4); las\n"
           "                         consultas se procesan de una en una.\n"
           "      La conexion a MySQL se toma de TT_DB_HOST, TT_DB_PORT, TT_DB_USER,\n"
           "      TT_DB_NAME y TT_DB_PASS (obligatoria). Ctrl+C o SIGTERM lo detienen.\n";
}

/*
 * Implementacion: carga el estado, comprueba MySQL (sin exigirlo: si no
 * responde, cada consulta que lea filas lo reintentara), abre el puerto y
 * atiende peticiones hasta recibir SIGINT o SIGTERM. Las senales se
 * bloquean antes de crear los hilos del servidor y las espera un hilo
 * dedicado (cli::run_until_signal), que es quien detiene el servidor.
 */
int run_serve(const std::vector<std::string>& args) {
    const ServeOptions opt = parse_serve_options(args);

    auto start = Clock::now();
    ProverState state;
    try {
        state = load_state(opt.state);
    } catch (const StateError& e) {
        std::fprintf(stderr, "[prover] %s\n", e.what());
        return 1;
    }
    std::printf("[prover] estado: version %llu, id %s (plan %s); delta y D~ cargados en %.2f s\n",
                static_cast<unsigned long long>(state.digest.state_version),
                crypto::to_hex(state.digest.id()).substr(0, 16).c_str(), state.digest.plan.c_str(),
                seconds_since(start));

    start = Clock::now();
    if (fs::exists(opt.keys / "public_key.txt")) {
        try {
            load_public_key(state, opt.keys);
        } catch (const StateError& e) {
            std::fprintf(stderr, "[prover] %s\n", e.what());
            return 1;
        }
        std::printf("[prover] clave publica cargada y comprobada contra delta en %.2f s\n", seconds_since(start));
    } else {
        std::fprintf(stderr,
                     "[prover] AVISO: no hay clave publica en %s; las consultas C7 a C14 se responderan con\n"
                     "         error hasta que se reinicie el servidor con --keys.\n",
                     opt.keys.string().c_str());
    }

    const mysql::ConnectionConfig db_config = mysql::ConnectionConfig::from_environment();
    try {
        mysql::Connection probe(db_config);  // tambien inicializa libmysqlclient antes de crear hilos
        std::printf("[prover] base D disponible: %s\n", db_config.describe().c_str());
    } catch (const mysql::DatabaseError& e) {
        std::fprintf(stderr,
                     "[prover] AVISO: por ahora no se pudo conectar a %s (%s);\n"
                     "         cada consulta que lea filas volvera a intentarlo.\n",
                     db_config.describe().c_str(), e.what());
    }
    mysql::DatabaseRowStore store(db_config);

    query::ProverContext context;
    context.database = &state.database;
    context.digest = &state.digest;
    context.public_key = state.public_key ? &state.public_key->aso : nullptr;
    context.store = &store;

    // Las senales se bloquean aqui para que los hilos del servidor las
    // hereden bloqueadas y solo el hilo vigilante las reciba.
    cli::block_stop_signals();

    net::ProverServer server(context, opt.server, log_request);
    const int port = server.bind();
    std::printf("[prover] escuchando en %s:%d (POST %s, GET %s); Ctrl+C para detener\n", opt.server.host.c_str(), port,
                net::kQueryPath, net::kHealthPath);

    cli::run_until_signal([&] { server.run(); }, [&] { server.stop(); }, "prover");
    std::printf("[prover] servidor detenido\n");
    return 0;
}

}  // namespace tt::prover

// ui_command.cpp - comando "tt_verifier ui"
//
// Interfaz del caso de estudio: levanta un servidor HTTP local con la
// pagina de ui/web. Cada consulta que el usuario hace en la pagina se envia
// al probador, se verifica aqui con delta y la clave publica (igual que en
// 'tt_verifier query') y la pagina solo muestra el veredicto y el resultado
// verificado. Se detiene con Ctrl+C o con SIGTERM.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "client_common.hpp"
#include "commands.hpp"
#include "tt/cli/cli.hpp"
#include "tt/net/prover_client.hpp"
#include "tt/net/protocol.hpp"
#include "tt/ui/portal.hpp"
#include "tt/ui/ui_server.hpp"

#ifndef TT_UI_DEFAULT_WEB_DIR
#define TT_UI_DEFAULT_WEB_DIR "ui/web"
#endif

namespace tt::verifier {

namespace {

namespace fs = std::filesystem;

/*
 * Opciones del comando ui.
 *
 * Campos:
 *   keys, delta      material publico del cliente (como en query)
 *   server           direccion del probador
 *   timeout_seconds  espera maxima por cada respuesta del probador
 *   http             interfaz, puerto y carpeta de la pagina
 */
struct UiOptions {
    fs::path keys = "keys";
    fs::path delta = "setup/delta.txt";
    net::Endpoint server;
    int timeout_seconds = 600;
    ui::UiServerOptions http;
};

/*
 * Lee un entero de una opcion (solo digitos, a lo mas 6).
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
 * Lee las opciones del comando; ui no recibe consulta (las escribe el
 * usuario en la pagina).
 *
 * Errores:
 *   Lanza cli::UsageError si una opcion es desconocida o no es valida.
 */
UiOptions parse_ui_options(const std::vector<std::string>& args) {
    UiOptions opt;
    opt.http.web_dir = TT_UI_DEFAULT_WEB_DIR;
    std::string server = default_prover_address();
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--keys") {
            opt.keys = cli::option_value(args, i);
        } else if (arg == "--delta") {
            opt.delta = cli::option_value(args, i);
        } else if (arg == "--server") {
            server = cli::option_value(args, i);
        } else if (arg == "--timeout") {
            opt.timeout_seconds = parse_int_option("--timeout", cli::option_value(args, i), 1, 86400);
        } else if (arg == "--host") {
            opt.http.host = cli::option_value(args, i);
        } else if (arg == "--port") {
            opt.http.port = parse_int_option("--port", cli::option_value(args, i), 1, 65535);
        } else if (arg == "--web") {
            opt.http.web_dir = cli::option_value(args, i);
        } else {
            throw cli::UsageError(arg.rfind("--", 0) == 0 ? "opcion desconocida: " + arg
                                                          : "ui no recibe consulta; se encontro '" + arg + "'");
        }
    }
    try {
        opt.server = net::parse_endpoint(server);
    } catch (const net::AddressError& e) {
        throw cli::UsageError(e.what());
    }
    return opt;
}

/*
 * Indica si la interfaz solo es accesible desde esta maquina.
 */
bool is_loopback(const std::string& host) {
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

/*
 * Imprime una consulta atendida en la bitacora de la interfaz.
 */
void log_query(const ui::QueryRecord& r) {
    if (r.outcome == "direct") {
        std::printf("[ui] \"%s\" -> SQL directo SIN verificar (HTTP %.2f s, %s)\n",
                    r.query.c_str(), r.prover_seconds, cli::size_text(r.bytes).c_str());
    } else if (r.outcome == "verified") {
        std::printf("[ui] \"%s\" -> verificada: %s; obtencion HTTP %.2f s (%s), verificacion %.2f s\n", r.query.c_str(),
                    r.note.c_str(), r.prover_seconds, cli::size_text(r.bytes).c_str(), r.verify_seconds);
    } else if (r.outcome == "rejected") {
        std::printf("[ui] \"%s\" -> RECHAZADA: %s\n", r.query.c_str(), r.note.c_str());
    } else if (r.outcome == "no_answer") {
        std::printf("[ui] \"%s\" -> sin respuesta del probador: %s\n", r.query.c_str(), r.note.c_str());
    } else if (r.outcome == "unsupported") {
        std::printf("[ui] \"%s\" -> no soportada: %s\n", r.query.c_str(), r.note.c_str());
    } else {
        std::printf("[ui] \"%s\" -> consulta mal escrita: %s\n", r.query.c_str(), r.note.c_str());
    }
}

/*
 * Imprime una peticion que termino en error (ruta inexistente, cabecera
 * faltante...).
 */
void log_http_error(const ui::HttpErrorRecord& r) {
    std::printf("[ui] %s %s %s -> %d\n", r.client.c_str(), r.method.c_str(), r.path.c_str(), r.status);
}

/*
 * Comprueba una vez al iniciar si el probador responde y con que estado, y
 * lo informa. No es obligatorio: la pagina lo vuelve a revisar.
 */
void report_prover(const net::Endpoint& server, const ClientState& state) {
    net::ClientOptions quick;
    quick.connect_timeout_seconds = 2;
    quick.read_timeout_seconds = 5;
    try {
        const net::HealthInfo info = net::fetch_health(server, quick);
        if (info.state_id == state.digest.id()) {
            std::printf("[ui] probador en %s: mismo estado que tu delta (id %s)\n", server.text().c_str(),
                        short_id(info.state_id).c_str());
        } else {
            std::fprintf(stderr,
                         "[ui] AVISO: el probador en %s responde con otro estado (id %s, tu delta es %s);\n"
                         "     todas sus respuestas se rechazaran.\n",
                         server.text().c_str(), short_id(info.state_id).c_str(),
                         short_id(state.digest.id()).c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ui] AVISO: por ahora el probador en %s no responde (%s); la pagina lo indicara.\n",
                     server.text().c_str(), net::safe_text(e.what()).c_str());
    }
}

}  // namespace

/*
 * Implementacion: el texto fijo del comando.
 */
std::string ui_usage() {
    return std::string(
               "  tt_verifier ui [--server <host:puerto>] [--keys <carpeta>] [--delta <archivo>]\n"
               "                 [--host <interfaz>] [--port <puerto>] [--web <carpeta>]\n"
               "                 [--timeout <s>]\n"
               "      Interfaz del caso de estudio en el navegador. Cada consulta de la pagina\n"
               "      se envia al probador y se verifica aqui con delta y la clave publica;\n"
               "      la pagina solo muestra el veredicto y el resultado verificado.\n"
               "      --server, --keys, --delta, --timeout  como en 'query'.\n"
               "      --host <interfaz>  donde escucha la interfaz (por defecto: 127.0.0.1,\n"
               "                         solo esta maquina).\n"
               "      --port <puerto>    puerto de la interfaz (por defecto: ") +
           std::to_string(ui::kDefaultUiPort) +
           ").\n"
           "      --web <carpeta>    carpeta de la pagina (por defecto: ui/web del codigo).\n"
           "      Ctrl+C o SIGTERM la detienen.\n";
}

/*
 * Implementacion: carga delta y la clave igual que query, informa el estado
 * del probador y atiende la pagina hasta recibir SIGINT o SIGTERM.
 */
int run_ui(const std::vector<std::string>& args) {
    const UiOptions opt = parse_ui_options(args);

    ClientState state;
    if (const int code = check_client_files(opt.keys, opt.delta); code != 0) return code;
    load_client_digest(opt.delta, state);
    if (const int code = load_client_key(opt.keys, state); code != 0) return code;
    report_prover(opt.server, state);

    ui::PortalOptions portal_options;
    portal_options.prover = opt.server;
    portal_options.query.read_timeout_seconds = opt.timeout_seconds;
    portal_options.health.connect_timeout_seconds = 2;
    portal_options.health.read_timeout_seconds = 5;
    const char* direct_env = std::getenv("TT_ENABLE_DIRECT_BENCH");
    portal_options.direct_benchmark = direct_env != nullptr && std::string(direct_env) == "1";
    if (portal_options.direct_benchmark) {
        std::printf("[ui] SQL directo habilitado para evaluacion, SIN verificacion criptografica\n");
    }
    ui::Portal portal(state.digest, state.public_key.aso, portal_options, log_query);

    // Las senales se bloquean antes de crear los hilos del servidor.
    cli::block_stop_signals();
    ui::UiServer server(portal, opt.http, log_http_error);
    const int port = server.bind();
    if (!is_loopback(opt.http.host)) {
        std::fprintf(stderr,
                     "[ui] AVISO: la interfaz escucha en %s; el veredicto viaja sin proteccion hasta el\n"
                     "     navegador. Usala solo en una red de confianza (por ejemplo, dentro de Docker).\n",
                     opt.http.host.c_str());
    }
    const std::string shown_host = opt.http.host == "0.0.0.0" ? "127.0.0.1" : opt.http.host;
    std::printf("[ui] interfaz lista en http://%s:%d (pagina: %s); Ctrl+C para detener\n", shown_host.c_str(), port,
                opt.http.web_dir.string().c_str());

    cli::run_until_signal([&] { server.run(); }, [&] { server.stop(); }, "ui");
    std::printf("[ui] interfaz detenida\n");
    return 0;
}

}  // namespace tt::verifier

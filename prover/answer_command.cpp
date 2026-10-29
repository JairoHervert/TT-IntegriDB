// answer_command.cpp - comando "tt_prover answer"
//
// Responde una sola consulta y guarda la respuesta en un archivo. Usa el
// mismo servicio del nucleo (query::answer_query) que el servidor de red,
// por lo que el archivo contiene exactamente los bytes que el probador
// enviaria por la red.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "commands.hpp"
#include "prover_state.hpp"
#include "tt/cli/cli.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/io/file.hpp"
#include "tt/mysql/connection.hpp"
#include "tt/mysql/row_store.hpp"
#include "tt/query/prover_service.hpp"
#include "tt/query/query.hpp"

namespace tt::prover {

namespace {

namespace fs = std::filesystem;
using cli::Clock;
using cli::seconds_since;

/*
 * Opciones del comando answer, ya leidas de la linea de comandos.
 *
 * Campos:
 *   query  la consulta a responder
 *   state  carpeta con delta.txt y authenticated_db.bin (lo que entrego el
 *          propietario)
 *   keys   carpeta con public_key.txt (solo para C7 a C14)
 *   out    archivo donde se escribe la respuesta
 */
struct AnswerOptions {
    query::ParsedQuery query;
    fs::path state = "setup";
    fs::path keys = "keys";
    fs::path out = "respuesta.bin";
};

/*
 * Lee las opciones: las palabras que no empiezan con "--" forman la
 * consulta; el resto son opciones con su valor.
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta no es valida o si una opcion es
 *   desconocida o le falta su valor.
 */
AnswerOptions parse_answer_options(const std::vector<std::string>& args) {
    AnswerOptions opt;
    std::vector<std::string> words;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--state") {
            opt.state = cli::option_value(args, i);
        } else if (arg == "--keys") {
            opt.keys = cli::option_value(args, i);
        } else if (arg == "--out") {
            opt.out = cli::option_value(args, i);
        } else if (arg.rfind("--", 0) == 0) {
            throw cli::UsageError("opcion desconocida: " + arg);
        } else {
            words.push_back(arg);
        }
    }
    try {
        opt.query = query::parse_query(words);
    } catch (const query::QueryError& e) {
        throw cli::UsageError(e.what());
    }
    return opt;
}

/*
 * Texto corto de un digest para los mensajes: sus primeros 16 digitos
 * hexadecimales.
 */
std::string short_id(const crypto::Digest& d) { return crypto::to_hex(d).substr(0, 16); }

}  // namespace

/*
 * Implementacion: el texto fijo del comando mas las formas de consulta del
 * nucleo.
 */
std::string answer_usage() {
    return std::string(
               "  tt_prover answer <consulta> [--state <carpeta>] [--keys <carpeta>] [--out <archivo>]\n"
               "      ProofGen de una sola consulta: genera la respuesta (R, pi) y la guarda\n"
               "      en un archivo (los mismos bytes que 'serve' enviaria por la red).\n"
               "      <consulta> es una de:\n") +
           query::kQueryForms +
           "      --state <carpeta>  donde estan delta.txt y authenticated_db.bin\n"
           "                         (por defecto: setup).\n"
           "      --keys <carpeta>   donde esta public_key.txt; solo la usan C7 a C14\n"
           "                         (por defecto: keys).\n"
           "      --out <archivo>    donde se escribe la respuesta (por defecto:\n"
           "                         respuesta.bin).\n"
           "      La conexion se toma de TT_DB_HOST, TT_DB_PORT, TT_DB_USER, TT_DB_NAME\n"
           "      y TT_DB_PASS (obligatoria para las consultas que leen filas de MySQL).\n";
}

/*
 * Implementacion: carga delta y D~ (y la clave publica si la consulta la
 * necesita), responde con query::answer_query leyendo de MySQL solo si la
 * consulta usa filas, y guarda los bytes. Avisa si alguna fila de la base
 * ya no es la que se autentico en Setup.
 */
int run_answer(const std::vector<std::string>& args) {
    const AnswerOptions opt = parse_answer_options(args);
    const auto t_total = Clock::now();
    const std::string label = opt.query.catalog_id.empty() ? "consulta" : "consulta " + opt.query.catalog_id;
    std::printf("[prover] %s: %s\n", label.c_str(), query::to_sql(opt.query).c_str());

    auto start = Clock::now();
    ProverState state;
    try {
        state = load_state(opt.state);
        if (query::needs_public_key(opt.query)) load_public_key(state, opt.keys);
    } catch (const StateError& e) {
        std::fprintf(stderr, "[prover] %s\n", e.what());
        return 1;
    }
    std::printf("[prover] estado: version %llu, id %s (plan %s); cargado en %.2f s\n",
                static_cast<unsigned long long>(state.digest.state_version), short_id(state.digest.id()).c_str(),
                state.digest.plan.c_str(), seconds_since(start));

    std::unique_ptr<mysql::DatabaseRowStore> store;
    if (query::reads_rows(opt.query)) {
        store = std::make_unique<mysql::DatabaseRowStore>(mysql::ConnectionConfig::from_environment());
    }
    query::ProverContext context;
    context.database = &state.database;
    context.digest = &state.digest;
    context.public_key = state.public_key ? &state.public_key->aso : nullptr;
    context.store = store.get();

    start = Clock::now();
    const query::ProverReply reply = query::answer_query(context, opt.query);
    const std::string source = store ? " (filas de " + store->description() + ")" : std::string();
    std::printf("[prover] %s; generada en %.2f s%s\n", reply.summary.c_str(), seconds_since(start), source.c_str());
    if (reply.stale_rows > 0) {
        std::fprintf(stderr,
                     "[prover] AVISO: %zu fila(s) de la base ya no son las que se autenticaron en Setup\n"
                     "         (la base cambio despues de Setup). Se envian tal como estan; el\n"
                     "         verificador rechazara la respuesta.\n",
                     reply.stale_rows);
    }

    start = Clock::now();
    if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
    io::write_file_atomic(opt.out, reply.bytes);
    std::printf("[prover] respuesta guardada en %.2f s: %s (%s)\n", seconds_since(start), opt.out.string().c_str(),
                cli::size_text(reply.bytes.size()).c_str());
    std::printf("[prover] tiempo total %.2f s\n", seconds_since(t_total));
    return 0;
}

}  // namespace tt::prover

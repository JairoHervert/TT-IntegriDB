// answer_command.cpp - comando "tt_prover answer"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "commands.hpp"
#include "tt/cli/cli.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/mysql/connection.hpp"
#include "tt/mysql/row_store.hpp"
#include "tt/query/query.hpp"
#include "tt/query/response_io.hpp"
#include "tt/query/selection.hpp"
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"

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
 *   out    archivo donde se escribe la respuesta
 */
struct AnswerOptions {
    query::ParsedQuery query;
    fs::path state = "setup";
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
               "  tt_prover answer <consulta> [--state <carpeta>] [--out <archivo>]\n"
               "      ProofGen: responde la consulta con las filas de MySQL y su prueba\n"
               "      (prueba del AIT y prueba de Merkle de cada fila), usando D~.\n"
               "      <consulta> es una de:\n") +
           query::kQueryForms +
           "      --state <carpeta>  donde estan delta.txt y authenticated_db.bin\n"
           "                         (por defecto: setup).\n"
           "      --out <archivo>    donde se escribe la respuesta (por defecto:\n"
           "                         respuesta.bin).\n"
           "      La conexion se toma de TT_DB_HOST, TT_DB_PORT, TT_DB_USER, TT_DB_NAME\n"
           "      y TT_DB_PASS (obligatoria).\n";
}

/*
 * Implementacion: carga delta y D~ (D~ se comprueba contra delta al
 * cargarse), responde con answer_selection leyendo las filas de MySQL y
 * guarda la respuesta. Avisa si alguna fila de la base ya no es la que se
 * autentico en Setup.
 */
int run_answer(const std::vector<std::string>& args) {
    const AnswerOptions opt = parse_answer_options(args);
    const fs::path delta_path = opt.state / "delta.txt";
    const fs::path adb_path = opt.state / "authenticated_db.bin";
    if (!fs::exists(delta_path) || !fs::exists(adb_path)) {
        std::fprintf(stderr,
                     "[prover] no hay estado autenticado en %s: el propietario debe ejecutar 'tt_owner setup'\n"
                     "         y entregar delta.txt y authenticated_db.bin.\n",
                     opt.state.string().c_str());
        return 1;
    }
    mysql::DatabaseRowStore store(mysql::ConnectionConfig::from_environment());
    const auto t_total = Clock::now();

    const std::string label = opt.query.catalog_id.empty() ? "consulta" : "consulta " + opt.query.catalog_id;
    std::printf("[prover] %s: %s\n", label.c_str(), query::to_sql(opt.query.query).c_str());

    auto start = Clock::now();
    const scheme::DatabaseDigest digest = scheme::load_digest(delta_path);
    const scheme::AuthenticatedDatabase adb = scheme::load_authenticated_db(adb_path, digest);
    std::printf("[prover] estado: version %llu, id %s (plan %s); D~ cargada en %.2f s\n",
                static_cast<unsigned long long>(digest.state_version), short_id(digest.id()).c_str(),
                digest.plan.c_str(), seconds_since(start));

    start = Clock::now();
    const query::SelectionAnswer answer = query::answer_selection(adb, digest, opt.query.query, store);
    const double prove_seconds = seconds_since(start);
    std::printf("[prover] AIT %s: %zu filas en el rango; prueba de %zu nodos\n", answer.tree.label().c_str(),
                answer.response.rows.size(), answer.response.range.entries.size());
    std::printf("[prover] filas leidas de %s y pruebas generadas en %.2f s\n", store.description().c_str(),
                prove_seconds);
    if (answer.stale_rows > 0) {
        std::fprintf(stderr,
                     "[prover] AVISO: %zu fila(s) de la base ya no son las que se autenticaron en Setup\n"
                     "         (la base cambio despues de Setup). Se envian tal como estan; el\n"
                     "         verificador rechazara la respuesta.\n",
                     answer.stale_rows);
    }

    start = Clock::now();
    if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
    const std::size_t bytes = query::save_response(opt.out, answer.response);
    std::printf("[prover] respuesta guardada en %.2f s: %s (%s)\n", seconds_since(start), opt.out.string().c_str(),
                cli::size_text(bytes).c_str());
    std::printf("[prover] tiempo total %.2f s\n", seconds_since(t_total));
    return 0;
}

}  // namespace tt::prover

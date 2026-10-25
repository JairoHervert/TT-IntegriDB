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
#include "tt/query/multirange.hpp"
#include "tt/query/minimum.hpp"
#include "tt/query/average.hpp"
#include "tt/query/join_program.hpp"
#include "tt/query/join_course.hpp"
#include "tt/query/sum_join_course.hpp"
#include "tt/query/count_intersection.hpp"
#include "tt/scheme/key_io.hpp"
#include "tt/scheme/keys.hpp"
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
    fs::path keys = "keys";
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
               "  tt_prover answer <consulta> [--state <carpeta>] [--out <archivo>]\n"
               "      ProofGen: responde la consulta con las filas de MySQL y su prueba\n"
               "      (prueba del AIT y prueba de Merkle de cada fila), usando D~.\n"
               "      <consulta> es una de:\n") +
           query::kQueryForms +
           "      --state <carpeta>  donde estan delta.txt y authenticated_db.bin\n"
           "                         (por defecto: setup).\n"
           "      --keys <carpeta>   clave publica ASO de C7/C11/C12/C13 (defecto: keys).\n"
           "      --out <archivo>    donde se escribe la respuesta (por defecto:\n"
           "                         respuesta.bin).\n"
           "      La conexion se toma de TT_DB_HOST, TT_DB_PORT, TT_DB_USER, TT_DB_NAME\n"
           "      y TT_DB_PASS (obligatoria para consultas que leen filas MySQL).\n";
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
    const auto t_total = Clock::now();

    const std::string label = opt.query.catalog_id.empty() ? "consulta" : "consulta " + opt.query.catalog_id;
    std::printf("[prover] %s: %s\n", label.c_str(),  (opt.query.join_courses ? "SELECT * FROM academic_record JOIN course ON academic_record.course_id = course.course_id" :
                                         opt.query.join_programs ? "SELECT * FROM student JOIN program ON student.program_id = program.program_id" :
                                         opt.query.avg_student ? ("SELECT AVG(grade) FROM academic_record WHERE student_id = " + std::to_string(*opt.query.avg_student)) :
                                         opt.query.min_student ? ("SELECT MIN(grade) FROM academic_record WHERE student_id = " + std::to_string(*opt.query.min_student)) :
                                         opt.query.sum_credits_student ? ("SELECT SUM(course.credits_tenths) FROM academic_record JOIN course ON academic_record.course_id = course.course_id WHERE academic_record.student_id = " + std::to_string(*opt.query.sum_credits_student) + " AND academic_record.grade >= 6") :
                                         opt.query.count ? query::to_sql(*opt.query.count) :
                                         opt.query.multi ? query::to_sql(*opt.query.multi) :
                                                           query::to_sql(opt.query.query)).c_str());

    auto start = Clock::now();
    const scheme::DatabaseDigest digest = scheme::load_digest(delta_path);
    const scheme::AuthenticatedDatabase adb = scheme::load_authenticated_db(adb_path, digest);
    std::printf("[prover] estado: version %llu, id %s (plan %s); D~ cargada en %.2f s\n",
                static_cast<unsigned long long>(digest.state_version), short_id(digest.id()).c_str(),
                digest.plan.c_str(), seconds_since(start));

    if (opt.query.sum_credits_student) {
        const auto pk = scheme::load_public_key(opt.keys / "public_key.txt");
        if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint)
            throw std::runtime_error("C14: clave publica distinta de delta");
        mysql::DatabaseRowStore store(mysql::ConnectionConfig::from_environment());
        start = Clock::now();
        const auto answer = query::answer_sum_join_course(adb, digest, pk.aso,
                                                          *opt.query.sum_credits_student, store);
        if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
        const auto bytes = query::save_sum_join_course_response(opt.out, answer.response);
        std::printf("[prover] C14: %zu registros aprobados, %zu cursos participantes; ASO en %.2f s\n",
                    answer.response.filtered.rows.size(), answer.response.course_rows.size(), seconds_since(start));
        if (answer.stale_rows)
            std::fprintf(stderr, "[prover] AVISO: %zu filas cambiaron despues de Setup\n", answer.stale_rows);
        std::printf("[prover] respuesta C14: %s (%s); total %.2f s\n",
                    opt.out.string().c_str(), cli::size_text(bytes).c_str(), seconds_since(t_total));
        return 0;
    }
    if (opt.query.join_courses) {
        const auto pk = scheme::load_public_key(opt.keys / "public_key.txt");
        if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint)
            throw std::runtime_error("C8: clave publica distinta de delta");
        mysql::DatabaseRowStore store(mysql::ConnectionConfig::from_environment());
        start = Clock::now();
        const auto answer = query::answer_join_course(adb, digest, pk.aso, store);
        if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
        const auto bytes = query::save_join_course_response(opt.out, answer.response);
        std::printf("[prover] C8: %zu registros, %zu cursos; JOIN de %zu filas; ASO en %.2f s\n",
                    answer.response.record_rows.size(), answer.response.course_rows.size(),
                    answer.response.record_rows.size(), seconds_since(start));
        if (answer.stale_rows)
            std::fprintf(stderr, "[prover] AVISO: %zu filas cambiaron despues de Setup\n", answer.stale_rows);
        std::printf("[prover] respuesta C8: %s (%s); total %.2f s\n",
                    opt.out.string().c_str(), cli::size_text(bytes).c_str(), seconds_since(t_total));
        return 0;
    }
    if (opt.query.join_programs) {
        const auto pk = scheme::load_public_key(opt.keys / "public_key.txt");
        if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint)
            throw std::runtime_error("C9: clave publica distinta de delta");
        mysql::DatabaseRowStore store(mysql::ConnectionConfig::from_environment());
        start = Clock::now();
        const auto answer = query::answer_join_program(adb, digest, pk.aso, store);
        if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
        const auto bytes = query::save_join_program_response(opt.out, answer.response);
        std::printf("[prover] C9: %zu alumnos, %zu programas; JOIN de %zu filas; ASO en %.2f s\n",
                    answer.response.student_rows.size(), answer.response.program_rows.size(),
                    answer.response.student_rows.size(), seconds_since(start));
        if (answer.stale_rows)
            std::fprintf(stderr, "[prover] AVISO: %zu filas cambiaron despues de Setup\n", answer.stale_rows);
        std::printf("[prover] respuesta C9: %s (%s); total %.2f s\n",
                    opt.out.string().c_str(), cli::size_text(bytes).c_str(), seconds_since(t_total));
        return 0;
    }
    if (opt.query.avg_student) {
        const auto pk = scheme::load_public_key(opt.keys / "public_key.txt");
        if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint)
            throw std::runtime_error("C10: clave publica distinta de delta");
        start = Clock::now();
        const auto resp = query::answer_average(adb, digest, pk.aso, *opt.query.avg_student);
        if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
        const auto bytes = query::save_average_response(opt.out, resp);
        if (resp.count)
            std::printf("[prover] C10: SUM %lld, COUNT %llu, AVG %.6f; ASO en %.2f s\n",
                        static_cast<long long>(resp.sum), static_cast<unsigned long long>(resp.count),
                        static_cast<double>(resp.sum) / resp.count, seconds_since(start));
        else std::printf("[prover] C10: AVG NULL, alumno sin registros\n");
        std::printf("[prover] respuesta C10: %s (%s), sin SELECT MySQL; total %.2f s\n",
                    opt.out.string().c_str(), cli::size_text(bytes).c_str(), seconds_since(t_total));
        return 0;
    }

    if (opt.query.min_student) {
        const auto pk = scheme::load_public_key(opt.keys / "public_key.txt");
        if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint) {
            throw std::runtime_error("clave publica de C13 distinta de delta");
        }
        mysql::DatabaseRowStore store(mysql::ConnectionConfig::from_environment());
        start = Clock::now();
        const auto answer = query::answer_minimum(adb, digest, pk.aso, *opt.query.min_student, store);
        const auto& r = answer.range_answer;
        std::printf("[prover] C13: alumno %llu refs, rango grade %llu refs, "
                    "MIN %s, %zu filas testigo; AIT+ASO+Merkle en %.2f s\n",
                    static_cast<unsigned long long>(r.input_counts[0]),
                    static_cast<unsigned long long>(r.input_counts[1]),
                    answer.minimum ? std::to_string(*answer.minimum).c_str() : "NULL",
                    r.response.rows.size(), seconds_since(start));
        if (r.stale_rows > 0) {
            std::fprintf(stderr, "[prover] AVISO: %zu filas distintas al estado de Setup\n", r.stale_rows);
        }
        if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
        const auto bytes = query::save_multirange_response(opt.out, r.response);
        std::printf("[prover] respuesta C13 guardada: %s (%s); total %.2f s\n",
                    opt.out.string().c_str(), cli::size_text(bytes).c_str(), seconds_since(t_total));
        return 0;
    }

    if (opt.query.count) {
        const fs::path pk_path = opt.keys / "public_key.txt";
        const auto pk = scheme::load_public_key(pk_path);
        if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint) {
            throw std::runtime_error("clave publica de COUNT distinta de delta");
        }
        start = Clock::now();
        const auto answer = query::answer_count(adb, digest, pk.aso, *opt.query.count);
        std::printf("[prover] %s: AIT alumno %llu, AIT segundo filtro %llu; COUNT %llu; ASO en %.2f s\n",
                    opt.query.catalog_id.c_str(),
                    static_cast<unsigned long long>(answer.input_counts[0]),
                    static_cast<unsigned long long>(answer.input_counts[1]),
                    static_cast<unsigned long long>(answer.response.count), seconds_since(start));
        if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
        const auto bytes = query::save_count_response(opt.out, answer.response);
        std::printf("[prover] respuesta %s guardada: %s (%s); sin SELECT MySQL; total %.2f s\n",
                    opt.query.catalog_id.c_str(), opt.out.string().c_str(),
                    cli::size_text(bytes).c_str(), seconds_since(t_total));
        return 0;
    }

    if (opt.query.multi) {
        mysql::DatabaseRowStore store(mysql::ConnectionConfig::from_environment());
        const fs::path pk_path = opt.keys / "public_key.txt";
        const auto pk = scheme::load_public_key(pk_path);
        if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint) {
            throw std::runtime_error("clave publica de C7 distinta de la publicada en delta");
        }
        start = Clock::now();
        const auto answer = query::answer_multirange(adb, digest, pk.aso, *opt.query.multi, store);
        const double proof_s = seconds_since(start);
        std::printf("[prover] C7: AIT grado %llu, AIT periodo %llu; interseccion %zu filas; prueba ASO en %.2f s\n",
                    static_cast<unsigned long long>(answer.input_counts[0]),
                    static_cast<unsigned long long>(answer.input_counts[1]),
                    answer.response.rows.size(), proof_s);
        if (answer.stale_rows) std::fprintf(stderr, "[prover] AVISO: %zu filas cambiaron despues de Setup\n", answer.stale_rows);
        if (opt.out.has_parent_path()) fs::create_directories(opt.out.parent_path());
        const auto bytes = query::save_multirange_response(opt.out, answer.response);
        std::printf("[prover] respuesta C7 guardada: %s (%s); tiempo total %.2f s\n",
                    opt.out.string().c_str(), cli::size_text(bytes).c_str(), seconds_since(t_total));
        return 0;
    }

    mysql::DatabaseRowStore store(mysql::ConnectionConfig::from_environment());
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

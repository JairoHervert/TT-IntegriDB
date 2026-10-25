// verify_command.cpp - comando "tt_verifier verify"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "commands.hpp"
#include "tt/cli/cli.hpp"
#include "tt/crypto/hash.hpp"
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
#include "tt/scheme/digest.hpp"
#include "tt/scheme/key_io.hpp"
#include "tt/scheme/keys.hpp"

namespace tt::verifier {

namespace {

namespace fs = std::filesystem;
using cli::Clock;
using cli::seconds_since;

/*
 * Opciones del comando verify, ya leidas de la linea de comandos.
 *
 * Campos:
 *   query     la consulta que hizo el cliente
 *   response  archivo con la respuesta del probador
 *   keys      carpeta con public_key.txt
 *   delta     archivo del digest delta vigente
 *   show      cuantas filas del resultado mostrar
 *   aso       si es true, ademas se comprueba el acumulador de cada nodo de
 *             cobertura (verificacion literal del articulo; ver
 *             query/selection.hpp)
 */
struct VerifyOptions {
    query::ParsedQuery query;
    fs::path response = "respuesta.bin";
    fs::path keys = "keys";
    fs::path delta = "setup/delta.txt";
    std::size_t show = 10;
    bool aso = false;
};

/*
 * Convierte el texto de --show en numero (solo digitos, a lo mas 9).
 *
 * Errores:
 *   Lanza cli::UsageError si no es un entero no negativo.
 */
std::size_t parse_show(const std::string& text) {
    if (text.empty() || text.size() > 9 || text.find_first_not_of("0123456789") != std::string::npos) {
        throw cli::UsageError("--show espera un entero no negativo, se recibio '" + text + "'");
    }
    return static_cast<std::size_t>(std::stoul(text));
}

/*
 * Lee las opciones: las palabras que no empiezan con "--" forman la
 * consulta; el resto son opciones con su valor.
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta no es valida o si una opcion es
 *   desconocida o le falta su valor.
 */
VerifyOptions parse_verify_options(const std::vector<std::string>& args) {
    VerifyOptions opt;
    std::vector<std::string> words;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--response") {
            opt.response = cli::option_value(args, i);
        } else if (arg == "--keys") {
            opt.keys = cli::option_value(args, i);
        } else if (arg == "--delta") {
            opt.delta = cli::option_value(args, i);
        } else if (arg == "--show") {
            opt.show = parse_show(cli::option_value(args, i));
        } else if (arg == "--aso") {
            opt.aso = true;
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
 * Texto de una celda para la tabla de resultados.
 */
std::string cell_text(const db::Cell& cell) {
    if (const auto* v = std::get_if<std::int64_t>(&cell)) return std::to_string(*v);
    return std::get<std::string>(cell);
}

/*
 * Imprime las primeras `limit` filas como tabla de texto, con el nombre de
 * cada columna como encabezado y columnas alineadas.
 */
void print_rows(const db::TableSchema& schema, const std::vector<db::Row>& rows, std::size_t limit) {
    const std::size_t shown = std::min(limit, rows.size());
    if (shown == 0) return;
    std::vector<std::size_t> width(schema.columns.size());
    for (std::size_t c = 0; c < schema.columns.size(); ++c) width[c] = schema.columns[c].name.size();
    for (std::size_t r = 0; r < shown; ++r) {
        for (std::size_t c = 0; c < width.size(); ++c) width[c] = std::max(width[c], cell_text(rows[r][c]).size());
    }
    auto print_line = [&](auto text_of) {
        std::string line = "  ";
        for (std::size_t c = 0; c < width.size(); ++c) {
            std::string t = text_of(c);
            t.resize(width[c], ' ');
            line += (c ? " | " : "") + t;
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        std::printf("%s\n", line.c_str());
    };
    print_line([&](std::size_t c) { return schema.columns[c].name; });
    print_line([&](std::size_t c) { return std::string(width[c], '-'); });
    for (std::size_t r = 0; r < shown; ++r) print_line([&](std::size_t c) { return cell_text(rows[r][c]); });
    if (shown < rows.size()) std::printf("  ... (%zu filas mas; usa --show N para ver mas)\n", rows.size() - shown);
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
std::string verify_usage() {
    return std::string(
               "  tt_verifier verify <consulta> [--response <archivo>] [--keys <carpeta>]\n"
               "                     [--delta <archivo>] [--show <n>] [--aso]\n"
               "      Verify: revisa la respuesta del probador a la consulta contra el\n"
               "      digest delta y la clave publica, y muestra el resultado verificado.\n"
               "      <consulta> es la misma que se le pidio al probador:\n") +
           query::kQueryForms +
           "      --response <archivo>  respuesta del probador (por defecto: respuesta.bin).\n"
           "      --keys <carpeta>      donde esta public_key.txt (por defecto: keys).\n"
           "      --delta <archivo>     digest delta vigente (por defecto: setup/delta.txt).\n"
           "      --show <n>            filas del resultado que se muestran (por defecto: 10).\n"
           "      --aso                 comprueba ademas el acumulador de cada nodo de\n"
           "                            cobertura, como el procedimiento del articulo (con\n"
           "                            los contadores autenticados es redundante y mas lento).\n"
           "      Codigo de salida: 0 verificada, 3 rechazada, 1 no se pudo verificar.\n";
}

/*
 * Implementacion: carga delta, revisa que la consulta se pueda verificar,
 * carga pk y exige que su huella sea la de delta; despues lee la respuesta
 * (si no tiene formato valido, se rechaza) y aplica verify_selection.
 */
int run_verify(const std::vector<std::string>& args) {
    const VerifyOptions opt = parse_verify_options(args);
    const query::RangeQuery& q = opt.query.query;
    const std::string label = opt.query.catalog_id.empty() ? "consulta" : "consulta " + opt.query.catalog_id;
    std::printf("[verifier] %s: %s\n", label.c_str(), (opt.query.join_courses ? "SELECT * FROM academic_record JOIN course ON academic_record.course_id = course.course_id" :
                                          opt.query.join_programs ? "SELECT * FROM student JOIN program ON student.program_id = program.program_id" :
                                          opt.query.avg_student ? ("SELECT AVG(grade) FROM academic_record WHERE student_id = " + std::to_string(*opt.query.avg_student)) :
                                          opt.query.min_student ? ("SELECT MIN(grade) FROM academic_record WHERE student_id = " + std::to_string(*opt.query.min_student)) :
                                          opt.query.sum_credits_student ? ("SELECT SUM(course.credits_tenths) FROM academic_record JOIN course ON academic_record.course_id = course.course_id WHERE academic_record.student_id = " + std::to_string(*opt.query.sum_credits_student) + " AND academic_record.grade >= 6") :
                                          opt.query.count ? query::to_sql(*opt.query.count) :
                                          opt.query.multi ? query::to_sql(*opt.query.multi) : query::to_sql(q)).c_str());

    const fs::path pk_path = opt.keys / "public_key.txt";
    if (!fs::exists(opt.delta)) {
        std::fprintf(stderr,
                     "[verifier] no existe el digest %s: indica con --delta el delta.txt que publico el\n"
                     "           propietario (por ejemplo --delta demo/setup/delta.txt).\n",
                     opt.delta.string().c_str());
        return 1;
    }
    if (!fs::exists(pk_path)) {
        std::fprintf(stderr,
                     "[verifier] no hay clave publica en %s: indica con --keys la carpeta donde\n"
                     "           'tt_owner init' guardo public_key.txt (la misma que se uso en Setup).\n",
                     opt.keys.string().c_str());
        return 1;
    }
    const scheme::DatabaseDigest digest = scheme::load_digest(opt.delta);
    std::printf("[verifier] digest delta: %s (version %llu, id %s)\n", opt.delta.string().c_str(),
                static_cast<unsigned long long>(digest.state_version), short_id(digest.id()).c_str());
    try {
        if (opt.query.sum_credits_student) {
            const auto q14 = query::c14_filter(*opt.query.sum_credits_student);
            for (const auto& part : {q14.first, q14.second}) {
                const auto spec = query::selection_tree(part, digest);
                std::printf("[verifier] C14: AIT %s\n", spec.label().c_str());
            }
            const scheme::TreeSpec course{"course", "course_id", "course_id"};
            if (!digest.find_tree(course)) throw query::UnsupportedQuery("C14: falta AIT " + course.label());
            std::printf("[verifier] C14: AIT %s\n", course.label().c_str());
        } else if (opt.query.join_courses) {
            for (const auto* name : {"academic_record", "course"}) {
                const scheme::TreeSpec spec{name, "course_id", "course_id"};
                if (!digest.find_tree(spec)) throw query::UnsupportedQuery("C8: falta AIT " + spec.label());
                std::printf("[verifier] C8: AIT %s\n", spec.label().c_str());
            }
        } else if (opt.query.join_programs) {
            for (const auto* name : {"student", "program"}) {
                const scheme::TreeSpec spec{name, "program_id", "program_id"};
                if (!digest.find_tree(spec)) throw query::UnsupportedQuery("C9: falta AIT " + spec.label());
                std::printf("[verifier] C9: AIT %s\n", spec.label().c_str());
            }
        } else if (opt.query.avg_student) {
            const scheme::TreeSpec spec{"academic_record", "student_id", "grade"};
            if (!digest.find_tree(spec)) throw query::UnsupportedQuery("C10: AIT no existe: " + spec.label());
            std::printf("[verifier] C10: AIT %s\n", spec.label().c_str());
        } else if (opt.query.min_student) {
            const query::RangeQuery a{"academic_record", "student_id", *opt.query.min_student, *opt.query.min_student};
            const query::RangeQuery b{"academic_record", "grade", 0, 0};
            for (const auto& part : {a, b}) {
                const auto spec = query::selection_tree(part, digest);
                std::printf("[verifier] C13: AIT %s\n", spec.label().c_str());
            }
        } else if (opt.query.count) {
            for (const auto& part : {opt.query.count->first, opt.query.count->second}) {
                const auto spec = query::selection_tree(part, digest);
                std::printf("[verifier] %s: AIT %s\n", opt.query.catalog_id.c_str(), spec.label().c_str());
            }
        } else if (opt.query.multi) {
            for (const auto& part : {opt.query.multi->first, opt.query.multi->second}) {
                const auto spec = query::selection_tree(part, digest);
                std::printf("[verifier] C7: AIT %s\n", spec.label().c_str());
            }
        } else {
            const scheme::TreeSpec spec = query::selection_tree(q, digest);
            std::printf("[verifier] se verifica con el AIT %s\n", spec.label().c_str());
        }
    } catch (const query::UnsupportedQuery& e) {
        std::fprintf(stderr, "[verifier] consulta no soportada: %s\n", e.what());
        return 1;
    }

    auto start = Clock::now();
    const scheme::PublicKey pk = scheme::load_public_key(pk_path);
    if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint) {
        std::fprintf(stderr,
                     "[verifier] la clave publica de %s no es la del digest delta: usa con --keys la\n"
                     "           carpeta del mismo 'tt_owner init' con el que se ejecuto ese Setup.\n",
                     opt.keys.string().c_str());
        return 1;
    }
    std::printf("[verifier] clave publica cargada y comprobada contra delta en %.2f s\n", seconds_since(start));

    if (!fs::exists(opt.response)) {
        std::fprintf(stderr, "[verifier] no existe la respuesta %s\n", opt.response.string().c_str());
        return 1;
    }
    std::printf("[verifier] respuesta del probador: %s (%s)\n", opt.response.string().c_str(),
                cli::file_size_text(opt.response).c_str());
    if (opt.query.sum_credits_student) {
        query::SumJoinCourseResponse response;
        try { response = query::load_sum_join_course_response(opt.response); }
        catch (const query::ResponseFormatError& e) {
            std::printf("[verifier] RESPUESTA RECHAZADA C14: %s\n", e.what());
            return kExitRejected;
        }
        start = Clock::now();
        const auto v = query::verify_sum_join_course(digest, pk.aso,
                                                    *opt.query.sum_credits_student, response);
        if (!v.accepted) {
            std::printf("[verifier] RESPUESTA RECHAZADA C14: %s\n", v.reason.c_str());
            return kExitRejected;
        }
        if (v.credits_tenths) {
            std::printf("[verifier] RESPUESTA VERIFICADA C14: SUM = %lld decimas de credito "
                        "(%.1f creditos); %zu filas JOIN; %zu cursos; en %.2f s\n",
                        static_cast<long long>(*v.credits_tenths), *v.credits_tenths / 10.0,
                        v.joined_rows, v.course_rows, seconds_since(start));
        } else {
            std::printf("[verifier] RESPUESTA VERIFICADA C14: SUM = NULL "
                        "(0 filas JOIN); en %.2f s\n", seconds_since(start));
        }
        return 0;
    }
    if (opt.query.join_courses) {
        query::JoinCourseResponse response;
        try { response = query::load_join_course_response(opt.response); }
        catch (const query::ResponseFormatError& e) {
            std::printf("[verifier] RESPUESTA RECHAZADA C8: %s\n", e.what());
            return kExitRejected;
        }
        start = Clock::now();
        const auto v = query::verify_join_course(digest, pk.aso, response);
        if (!v.accepted) {
            std::printf("[verifier] RESPUESTA RECHAZADA C8: %s\n", v.reason.c_str());
            return kExitRejected;
        }
        std::printf("[verifier] RESPUESTA VERIFICADA C8: %zu filas JOIN; %zu cursos en %.2f s\n",
                    v.joined_rows, v.course_rows.size(), seconds_since(start));
        const auto& record_schema = digest.find_table("academic_record")->schema;
        const auto& course_schema = digest.find_table("course")->schema;
        db::TableSchema joined_schema;
        joined_schema.name = "academic_record JOIN course";
        for (const auto& c : record_schema.columns)
            joined_schema.columns.push_back({"academic_record." + c.name, c.type});
        for (const auto& c : course_schema.columns)
            joined_schema.columns.push_back({"course." + c.name, c.type});
        const std::size_t record_key = *record_schema.column_index("course_id");
        const std::size_t course_key = *course_schema.column_index("course_id");
        std::vector<db::Row> joined_rows;
        joined_rows.reserve(v.record_rows.size());
        for (const auto& record : v.record_rows) {
            const auto key = std::get<std::int64_t>(record.at(record_key));
            for (const auto& course : v.course_rows) {
                if (std::get<std::int64_t>(course.at(course_key)) != key) continue;
                db::Row joined = record;
                joined.insert(joined.end(), course.begin(), course.end());
                joined_rows.push_back(std::move(joined));
                break;
            }
        }
        print_rows(joined_schema, joined_rows, opt.show);
        return 0;
    }
    if (opt.query.join_programs) {
        query::JoinProgramResponse response;
        try { response = query::load_join_program_response(opt.response); }
        catch (const query::ResponseFormatError& e) {
            std::printf("[verifier] RESPUESTA RECHAZADA C9: %s\n", e.what());
            return kExitRejected;
        }
        start = Clock::now();
        const auto v = query::verify_join_program(digest, pk.aso, response);
        if (!v.accepted) {
            std::printf("[verifier] RESPUESTA RECHAZADA C9: %s\n", v.reason.c_str());
            return kExitRejected;
        }
        std::printf("[verifier] RESPUESTA VERIFICADA C9: %zu filas del JOIN; %zu programas en %.2f s\n",
                    v.joined_rows, v.program_rows.size(), seconds_since(start));
        // La salida del verificador es la del JOIN SQL, no dos tablas separadas.
        const auto& st_schema = digest.find_table("student")->schema;
        const auto& pr_schema = digest.find_table("program")->schema;
        db::TableSchema joined_schema;
        joined_schema.name = "student JOIN program";
        for (const auto& c : st_schema.columns)
            joined_schema.columns.push_back({"student." + c.name, c.type});
        for (const auto& c : pr_schema.columns)
            joined_schema.columns.push_back({"program." + c.name, c.type});
        const std::size_t st_key = *st_schema.column_index("program_id");
        const std::size_t pr_key = *pr_schema.column_index("program_id");
        std::vector<db::Row> joined_rows;
        joined_rows.reserve(v.student_rows.size());
        for (const auto& student : v.student_rows) {
            const auto key = std::get<std::int64_t>(student.at(st_key));
            for (const auto& program : v.program_rows) {
                if (std::get<std::int64_t>(program.at(pr_key)) != key) continue;
                db::Row joined = student;
                joined.insert(joined.end(), program.begin(), program.end());
                joined_rows.push_back(std::move(joined));
                break;
            }
        }
        print_rows(joined_schema, joined_rows, opt.show);
        return 0;
    }
    if (opt.query.avg_student) {
        query::AverageResponse response;
        try { response = query::load_average_response(opt.response); }
        catch (const query::ResponseFormatError& e) {
            std::printf("[verifier] RESPUESTA RECHAZADA C10: %s\n", e.what());
            return kExitRejected;
        }
        start = Clock::now();
        const auto checked = query::verify_average(digest, pk.aso, *opt.query.avg_student, response);
        if (!checked.accepted) {
            std::printf("[verifier] RESPUESTA RECHAZADA C10: %s\n", checked.reason.c_str());
            return kExitRejected;
        }
        if (checked.count)
            std::printf("[verifier] RESPUESTA VERIFICADA C10: SUM %lld / COUNT %llu = AVG %.6f en %.2f s\n",
                        static_cast<long long>(checked.sum), static_cast<unsigned long long>(checked.count),
                        static_cast<double>(checked.sum) / checked.count, seconds_since(start));
        else std::printf("[verifier] RESPUESTA VERIFICADA C10: AVG = NULL en %.2f s\n", seconds_since(start));
        return 0;
    }
    if (opt.query.min_student) {
        query::MultiRangeResponse response;
        try {
            response = query::load_multirange_response(opt.response);
        } catch (const query::ResponseFormatError& e) {
            std::printf("[verifier] RESPUESTA RECHAZADA C13: %s\n", e.what());
            return kExitRejected;
        }
        start = Clock::now();
        const auto checked = query::verify_minimum(digest, pk.aso, *opt.query.min_student, response);
        if (!checked.accepted) {
            std::printf("[verifier] RESPUESTA RECHAZADA C13: %s\n", checked.reason.c_str());
            return kExitRejected;
        }
        if (checked.minimum) {
            std::printf("[verifier] RESPUESTA VERIFICADA C13: MIN = %lld (%zu filas testigo) en %.2f s\n",
                        static_cast<long long>(*checked.minimum), checked.witnesses_rows, seconds_since(start));
        } else {
            std::printf("[verifier] RESPUESTA VERIFICADA C13: MIN = NULL (alumno sin registros) en %.2f s\n",
                        seconds_since(start));
        }
        return 0;
    }
    if (opt.query.count) {
        query::CountResponse response;
        try {
            response = query::load_count_response(opt.response);
        } catch (const query::ResponseFormatError& e) {
            std::printf("[verifier] RESPUESTA RECHAZADA %s: %s\n", opt.query.catalog_id.c_str(), e.what());
            return kExitRejected;
        }
        start = Clock::now();
        const auto verified = query::verify_count(digest, pk.aso, *opt.query.count, response);
        if (!verified.accepted) {
            std::printf("[verifier] RESPUESTA RECHAZADA %s: %s\n", opt.query.catalog_id.c_str(), verified.reason.c_str());
            return kExitRejected;
        }
        std::printf("[verifier] RESPUESTA VERIFICADA %s: COUNT = %llu en %.2f s\n",
                    opt.query.catalog_id.c_str(), static_cast<unsigned long long>(verified.count), seconds_since(start));
        return 0;
    }
    if (opt.query.multi) {
        query::MultiRangeResponse response;
        try {
            response = query::load_multirange_response(opt.response);
        } catch (const query::ResponseFormatError& e) {
            std::printf("[verifier] RESPUESTA RECHAZADA: %s\n", e.what());
            return kExitRejected;
        }
        start = Clock::now();
        const auto v = query::verify_multirange(digest, pk.aso, *opt.query.multi, response);
        if (!v.accepted) {
            std::printf("[verifier] RESPUESTA RECHAZADA C7: %s\n", v.reason.c_str());
            return kExitRejected;
        }
        std::printf("[verifier] RESPUESTA VERIFICADA C7: %zu filas en %.2f s\n",
                    v.rows.size(), seconds_since(start));
        std::printf("[verifier] AIT %.3f s, Merkle %.3f s, ASO %.3f s\n",
                    v.range_seconds, v.rows_seconds, v.aso_seconds);
        print_rows(digest.find_table(q.table)->schema, v.rows, opt.show);
        return 0;
    }
    query::SelectionResponse response;
    try {
        response = query::load_response(opt.response);
    } catch (const query::ResponseFormatError& e) {
        std::printf("[verifier] RESPUESTA RECHAZADA: %s\n", e.what());
        return kExitRejected;
    }

    start = Clock::now();
    const query::Check check = opt.aso ? query::Check::kWithAccumulators : query::Check::kCounts;
    const query::SelectionVerification v = query::verify_selection(digest, pk.aso, q, response, check);
    const double total = seconds_since(start);
    if (!v.accepted) {
        std::printf("[verifier] RESPUESTA RECHAZADA: %s\n", v.reason.c_str());
        return kExitRejected;
    }
    std::printf("[verifier] RESPUESTA VERIFICADA: %zu filas (%zu nodos de cobertura) en %.2f s\n", v.rows.size(),
                v.cover_nodes, total);
    std::printf("[verifier]   prueba del AIT %.3f s, filas (hash + Merkle + contadores) %.3f s", v.times.range,
                v.times.rows);
    if (opt.aso) std::printf(", acumuladores (ASO) %.3f s", v.times.accumulators);
    std::printf("\n");
    print_rows(digest.find_table(q.table)->schema, v.rows, opt.show);
    return 0;
}

}  // namespace tt::verifier

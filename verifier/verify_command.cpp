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
    std::printf("[verifier] %s: %s\n", label.c_str(), (opt.query.min_student ? ("SELECT MIN(grade) FROM academic_record WHERE student_id = " + std::to_string(*opt.query.min_student)) :
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
        if (opt.query.min_student) {
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

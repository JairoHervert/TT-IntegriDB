// selection_test.cpp
//
// Pruebas de ProofGen y Verify para las consultas de seleccion y de rango
// (C1 a C6) sobre una base pequena con el esquema del caso de estudio:
// respuestas honestas comparadas con fuerza bruta, el formato binario de la
// respuesta, un probador malicioso (toda alteracion se rechaza) y una base
// modificada en el servidor despues de Setup.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/db/row_store.hpp"
#include "tt/query/query.hpp"
#include "tt/query/response_io.hpp"
#include "tt/query/selection.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace {

using namespace tt;
using query::Check;
using query::RangeQuery;
using query::SelectionResponse;
using test::check;
using test::check_throws;
using test::section;

constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

/*
 * Estado autenticado de una base: claves, delta y D~ con el plan 'queries'.
 */
struct State {
    scheme::KeyPair keys;
    scheme::SetupResult setup;
};

/*
 * Ejecuta Setup con el plan 'queries' sobre la base con las claves dadas.
 */
scheme::SetupResult run_setup(const db::Database& d, const scheme::KeyPair& keys) {
    std::vector<db::TableSchema> schemas;
    for (const db::Table& t : d.tables) schemas.push_back(t.schema);
    std::vector<scheme::TreeSpec> specs;
    for (const auto& p : scheme::plan_trees(scheme::Preset::kQueries, schemas)) specs.push_back(p.spec);
    return scheme::setup(d, specs, "queries", keys.sk, keys.pk);
}

/*
 * Busca una tabla de la base de prueba por nombre (para modificarla).
 */
db::Table& table_named(db::Database& d, const std::string& name) {
    for (db::Table& t : d.tables) {
        if (t.schema.name == name) return t;
    }
    throw std::logic_error("tabla de prueba inexistente: " + name);
}

/*
 * Resultado correcto de una consulta, calculado recorriendo todas las filas:
 * las que tienen la columna en [lo, hi], ordenadas por esa columna y luego
 * por la referencia (el orden del AIT).
 */
std::vector<db::Row> brute_force(const db::Database& d, const RangeQuery& q) {
    const db::Table& t = *d.find(q.table);
    const std::size_t key = *t.schema.column_index(q.column);
    const std::size_t ref = t.schema.reference_column;
    std::vector<db::Row> rows;
    for (const db::Row& r : t.rows) {
        const std::int64_t k = std::get<std::int64_t>(r[key]);
        if (q.lo <= k && k <= q.hi) rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [&](const db::Row& a, const db::Row& b) {
        return std::make_tuple(std::get<std::int64_t>(a[key]), std::get<std::int64_t>(a[ref])) <
               std::make_tuple(std::get<std::int64_t>(b[key]), std::get<std::int64_t>(b[ref]));
    });
    return rows;
}

/*
 * Lee una consulta escrita como en la linea de comandos.
 */
RangeQuery parse(const std::vector<std::string>& words) { return query::parse_query(words).query; }

}  // namespace

/*
 * Pruebas de seleccion y rango: honestas, formato, ataques y base alterada.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    const db::Database base = test::case_study_database(200);
    const scheme::KeyPair keys = scheme::init(256);
    const scheme::SetupResult state = run_setup(base, keys);
    const scheme::DatabaseDigest& digest = state.digest;
    const scheme::AuthenticatedDatabase& adb = state.database;
    const aso::PublicKey& pk = keys.pk.aso;
    db::MemoryRowStore store(base);

    auto answer = [&](const RangeQuery& q) { return query::answer_selection(adb, digest, q, store).response; };
    auto accepted = [&](const RangeQuery& q, const SelectionResponse& r, Check c) {
        return query::verify_selection(digest, pk, q, r, c).accepted;
    };
    // Una respuesta se rechaza si se rechaza en los dos modos de verificacion.
    auto rejected = [&](const RangeQuery& q, const SelectionResponse& r) {
        return !accepted(q, r, Check::kCounts) && !accepted(q, r, Check::kWithAccumulators);
    };
    auto reason = [&](const RangeQuery& q, const SelectionResponse& r) {
        return query::verify_selection(digest, pk, q, r).reason;
    };

    // -----------------------------------------------------------------------
    section("Consultas C1 a C6 del catalogo contra fuerza bruta");
    {
        const std::vector<std::vector<std::string>> catalog = {
            {"C1", "2024630003"}, {"C2", "2"},          {"C3", "4"},
            {"C4", "6", "8"},     {"C5", "20242", "20251"}, {"C6", "20241", "20242"},
        };
        for (const auto& words : catalog) {
            const RangeQuery q = parse(words);
            const query::SelectionAnswer a = query::answer_selection(adb, digest, q, store);
            const query::SelectionVerification v = query::verify_selection(digest, pk, q, a.response);
            const std::vector<db::Row> expected = brute_force(base, q);
            check(v.accepted && v.rows == expected && !expected.empty() && a.stale_rows == 0 &&
                      accepted(q, a.response, Check::kWithAccumulators),
                  words[0] + ": " + query::to_sql(q) + " -> " + std::to_string(expected.size()) +
                      " filas, iguales a la fuerza bruta (con y sin ASO)");
        }
    }

    // -----------------------------------------------------------------------
    section("Rangos aleatorios contra fuerza bruta");
    {
        struct Column {
            const char* table;
            const char* column;
            std::int64_t lo;
            std::int64_t hi;
        };
        const Column columns[] = {
            {"academic_record", "grade", 3, 12},           {"academic_record", "term_code", 20235, 20255},
            {"academic_record", "student_id", 0, 9},       {"academic_record", "evaluation_type", 0, 6},
            {"course", "course_id", 0, 7},                 {"student", "admission_term", 20235, 20255},
            {"student", "enrollment_number", 2024629999, 2024630009},
        };
        std::mt19937_64 rng(7);
        int all_ok = 0;
        int with_aso_ok = 0;
        const int rounds = 300;
        for (int i = 0; i < rounds; ++i) {
            const Column& c = columns[rng() % (sizeof columns / sizeof columns[0])];
            std::int64_t a = c.lo + static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(c.hi - c.lo + 1));
            std::int64_t b = c.lo + static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(c.hi - c.lo + 1));
            if (a > b) std::swap(a, b);
            const RangeQuery q{c.table, c.column, a, b};
            const SelectionResponse r = answer(q);
            const query::SelectionVerification v = query::verify_selection(digest, pk, q, r);
            if (v.accepted && v.rows == brute_force(base, q)) ++all_ok;
            if (i % 10 == 0 && accepted(q, r, Check::kWithAccumulators)) ++with_aso_ok;
        }
        check(all_ok == rounds, "300 rangos en 7 columnas: todas las respuestas se aceptan y coinciden");
        check(with_aso_ok == rounds / 10,
              "con la comprobacion del ASO tambien se aceptan (el AIT y las filas son coherentes)");
    }

    // -----------------------------------------------------------------------
    section("Resultados vacios y casos limite");
    {
        const RangeQuery none{"academic_record", "grade", 11, 20};
        const SelectionResponse r = answer(none);
        const query::SelectionVerification v = query::verify_selection(digest, pk, none, r);
        check(v.accepted && v.rows.empty() && r.rows.empty(), "rango sin filas: resultado vacio verificado");

        const RangeQuery missing = parse({"C1", "1999000000"});
        check(accepted(missing, answer(missing), Check::kCounts) && answer(missing).rows.empty(),
              "seleccion de una boleta que no existe: resultado vacio verificado");

        const RangeQuery no_records = parse({"C2", "8"});
        check(accepted(no_records, answer(no_records), Check::kCounts) && answer(no_records).rows.empty(),
              "el alumno 8 no tiene registros: resultado vacio verificado");

        const RangeQuery all{"academic_record", "grade", kMin, kMax};
        const query::SelectionVerification va = query::verify_selection(digest, pk, all, answer(all));
        check(va.accepted && va.rows.size() == 200 && va.cover_nodes == 1,
              "rango que abarca todo: las 200 filas con un solo nodo de cobertura (la raiz)");
    }

    // -----------------------------------------------------------------------
    section("Formato binario de la respuesta");
    const RangeQuery q = parse({"C4", "6", "8"});
    const SelectionResponse honest = answer(q);
    {
        const std::vector<unsigned char> bytes = query::encode_response(honest);
        const SelectionResponse decoded = query::decode_response(bytes);
        check(query::encode_response(decoded) == bytes && accepted(q, decoded, Check::kCounts),
              "leer lo escrito da la misma respuesta y se sigue verificando");

        const SelectionResponse text_rows = answer(parse({"C6", "20241", "20251"}));
        check(query::encode_response(query::decode_response(query::encode_response(text_rows))) ==
                  query::encode_response(text_rows),
              "filas con celdas de texto (student) se leen igual");

        std::vector<unsigned char> b = bytes;
        b.resize(b.size() - 1);
        check_throws<query::ResponseFormatError>([&] { query::decode_response(b); }, "respuesta truncada");
        b = bytes;
        b.push_back(0);
        check_throws<query::ResponseFormatError>([&] { query::decode_response(b); }, "bytes de sobra al final");
        b = bytes;
        b[0] = 'X';
        check_throws<query::ResponseFormatError>([&] { query::decode_response(b); }, "no es una respuesta");
        b = bytes;
        b[12] = 9;  // tipo de respuesta
        check_throws<query::ResponseFormatError>([&] { query::decode_response(b); }, "tipo de respuesta desconocido");
        check_throws<query::ResponseFormatError>([&] { query::decode_response({}); }, "respuesta vacia");
        b = bytes;
        // Encabezado (8 + 4 + 1), id (32), "academic_record" (4 + 15), "grade" (4 + 5), lo y hi (8 + 8):
        // en la posicion 89 empieza el numero de entradas de la prueba.
        for (std::size_t i = 89; i < 93; ++i) b[i] = 0xff;
        check_throws<query::ResponseFormatError>([&] { query::decode_response(b); },
                                                 "numero de entradas mayor que los datos (no reserva memoria)");
        b = bytes;
        b.resize(b.size() / 2);
        check_throws<query::ResponseFormatError>([&] { query::decode_response(b); }, "respuesta cortada a la mitad");
    }

    // -----------------------------------------------------------------------
    section("Probador malicioso: toda alteracion se rechaza (con y sin ASO)");
    {
        check(accepted(q, honest, Check::kCounts) && honest.rows.size() >= 4,
              "(control) la respuesta honesta de C4 6 8 se acepta");
        const scheme::AuthenticatedTable& ar = *adb.find_table("academic_record");
        const std::size_t grade = *ar.schema.column_index("grade");
        const std::size_t course = *ar.schema.column_index("course_id");

        // Una fila autentica de fuera del rango, con su prueba de Merkle real.
        query::RowProof outside;
        for (const db::Row& row : base.find("academic_record")->rows) {
            if (std::get<std::int64_t>(row[grade]) == 5) {
                outside.row = row;
                outside.inclusion = ar.rows.prove(*ar.position_of(std::get<std::int64_t>(row[0])));
                break;
            }
        }

        SelectionResponse r = honest;
        r.rows.pop_back();
        check(rejected(q, r), "omitir una fila (" + reason(q, r) + ")");

        r = honest;
        r.rows.back() = outside;
        check(rejected(q, r), "cambiar una fila por otra autentica de fuera del rango (" + reason(q, r) + ")");

        r = honest;
        r.rows.push_back(outside);
        check(rejected(q, r), "agregar una fila autentica de fuera del rango");

        r = honest;
        r.rows[1] = r.rows[0];
        check(rejected(q, r), "repetir una fila en lugar de otra (" + reason(q, r) + ")");

        r = honest;
        r.rows[0].row[course] = std::get<std::int64_t>(r.rows[0].row[course]) + 1;
        check(rejected(q, r), "cambiar una celda de una fila (" + reason(q, r) + ")");

        r = honest;
        r.rows[0].row[grade] = std::int64_t{7};
        check(rejected(q, r), "cambiar la calificacion por otra dentro del rango");

        r = honest;
        std::swap(r.rows[0].inclusion, r.rows[1].inclusion);
        check(rejected(q, r), "intercambiar las pruebas de Merkle de dos filas");

        r = honest;
        r.rows[0].inclusion.index = ar.rows.size();
        check(rejected(q, r), "prueba de Merkle con una posicion fuera del arbol");

        r = honest;
        r.rows[0].row.pop_back();
        check(rejected(q, r), "fila con una celda de menos (" + reason(q, r) + ")");

        r = honest;
        r.rows[0].row[course] = std::string("110");
        check(rejected(q, r), "celda de texto en una columna entera");

        r = honest;
        std::swap(r.rows.front(), r.rows.back());
        check(rejected(q, r), "mover filas entre nodos de cobertura (" + reason(q, r) + ")");

        r = honest;
        r.state_id[0] ^= 1;
        check(rejected(q, r), "respuesta de otro estado de la base (" + reason(q, r) + ")");

        const RangeQuery wider{"academic_record", "grade", 6, 9};
        check(rejected(wider, honest), "usar la respuesta de [6, 8] para la consulta [6, 9]");
        r = honest;
        r.query = wider;
        check(rejected(wider, r), "la misma respuesta, reescribiendo la consulta como [6, 9] (" + reason(wider, r) + ")");

        r = honest;
        r.range.entries.pop_back();
        check(rejected(q, r), "prueba del AIT incompleta");

        r = honest;
        r.rows.clear();
        r.range.entries.clear();  // la prueba de un arbol sin filas
        check(rejected(q, r), "decir que no hay resultados, con la prueba de un arbol vacio (" + reason(q, r) + ")");

        r = honest;
        r.rows.clear();
        check(rejected(q, r), "decir que no hay resultados, con la prueba honesta del AIT");

        r = honest;
        r.query = RangeQuery{"academic_record", "grade", 1, 2};
        check(rejected(q, r), "respuesta que dice responder otra consulta (" + reason(q, r) + ")");

        SelectionResponse text = answer(parse({"C6", "20241", "20242"}));
        const RangeQuery q6 = parse({"C6", "20241", "20242"});
        text.rows[0].row[2] = std::string("Otro nombre");
        check(rejected(q6, text), "cambiar el nombre de un alumno (celda de texto) en C6");

        // Estado nuevo: se agrega un registro dentro del rango y se repite Setup.
        db::Database grown = base;
        table_named(grown, "academic_record").rows.push_back({std::int64_t{201}, std::int64_t{1}, std::int64_t{1},
                                                             std::int64_t{20252}, std::int64_t{1}, std::int64_t{7}});
        const scheme::SetupResult next = run_setup(grown, keys);
        db::MemoryRowStore grown_store(grown);
        const SelectionResponse fresh = query::answer_selection(next.database, next.digest, q, grown_store).response;
        check(query::verify_selection(next.digest, pk, q, fresh).accepted && !accepted(q, fresh, Check::kCounts),
              "una respuesta del estado nuevo no se acepta con el delta anterior");
        check(!query::verify_selection(next.digest, pk, q, honest).accepted,
              "una respuesta del estado anterior no se acepta con el delta nuevo (RNF08)");
    }

    // -----------------------------------------------------------------------
    section("Base modificada en el servidor despues de Setup");
    {
        const std::size_t grade = 5;
        // La primera fila del resultado de C4 6 8.
        const std::int64_t target = std::get<std::int64_t>(honest.rows[0].row[0]);
        auto modified = [&](std::int64_t new_grade) {
            db::Database d = base;
            for (db::Row& row : table_named(d, "academic_record").rows) {
                if (std::get<std::int64_t>(row[0]) == target) row[grade] = new_grade;
            }
            return d;
        };

        db::MemoryRowStore inside(modified(7));
        const query::SelectionAnswer a = query::answer_selection(adb, digest, q, inside);
        const query::SelectionVerification v = query::verify_selection(digest, pk, q, a.response);
        check(a.stale_rows == 1 && !v.accepted,
              "una calificacion cambiada dentro del rango: el probador lo avisa y el verificador rechaza (" +
                  v.reason + ")");

        db::MemoryRowStore moved_out(modified(10));
        check(rejected(q, query::answer_selection(adb, digest, q, moved_out).response),
              "una calificacion cambiada a un valor fuera del rango: se rechaza");

        db::Database deleted = base;
        auto& rows = table_named(deleted, "academic_record").rows;
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&](const db::Row& row) { return std::get<std::int64_t>(row[0]) == target; }),
                   rows.end());
        db::MemoryRowStore gone(deleted);
        check_throws<db::DataError>([&] { query::answer_selection(adb, digest, q, gone); },
                                    "una fila borrada: el probador no puede responder");
    }

    // -----------------------------------------------------------------------
    section("Consultas no soportadas");
    {
        const RangeQuery no_tree{"student", "program_id", 1, 2};
        check_throws<query::UnsupportedQuery>([&] { answer(no_tree); },
                                              "rango sin AIT en delta: el probador no responde");
        SelectionResponse r = honest;
        r.query = no_tree;  // un servidor que finge responder la consulta sin prueba valida
        const query::SelectionVerification v = query::verify_selection(digest, pk, no_tree, r);
        check(!v.accepted && v.reason.find("no soportada") != std::string::npos,
              "y el verificador nunca la acepta (RNF05)");
        check_throws<query::UnsupportedQuery>([&] { answer(RangeQuery{"student", "first_name", 1, 2}); },
                                              "rango sobre una columna de texto");
        check_throws<query::UnsupportedQuery>([&] { answer(RangeQuery{"teacher", "id", 1, 2}); },
                                              "tabla que no existe");
    }

    return test::finish();
}

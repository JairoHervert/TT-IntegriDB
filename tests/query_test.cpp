// query_test.cpp
//
// Pruebas de la lectura de consultas (C1 a C6 y "range"), de su texto SQL y
// de la eleccion del AIT con el que se verifica cada una.

#include <limits>
#include <string>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/query/query.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace {

using namespace tt;
using query::QueryError;
using query::RangeQuery;
using query::UnsupportedQuery;
using test::check;
using test::check_throws;
using test::section;

/*
 * Lee una consulta escrita como en la linea de comandos.
 */
query::ParsedQuery parse(const std::vector<std::string>& words) { return query::parse_query(words); }

/*
 * Digest de la base de prueba con el plan indicado.
 */
scheme::DatabaseDigest digest_for(scheme::Preset preset, const scheme::KeyPair& keys) {
    const db::Database d = test::case_study_database(20);
    std::vector<scheme::TreeSpec> specs;
    for (const auto& p : scheme::plan_trees(preset, test::case_study_schemas())) specs.push_back(p.spec);
    return scheme::setup(d, specs, preset == scheme::Preset::kQueries ? "queries" : "full", keys.sk, keys.pk).digest;
}

}  // namespace

/*
 * Pruebas de consultas.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    section("Formas de consulta");
    {
        const query::ParsedQuery c1 = parse({"C1", "2024630001"});
        check(c1.catalog_id == "C1" && c1.query == RangeQuery{"student", "enrollment_number", 2024630001, 2024630001},
              "C1 <boleta>: seleccion en student.enrollment_number");
        check(parse({"C2", "7"}).query == RangeQuery{"academic_record", "student_id", 7, 7}, "C2 <student_id>");
        check(parse({"C3", "110"}).query == RangeQuery{"course", "course_id", 110, 110}, "C3 <course_id>");
        check(parse({"C4", "6", "8"}).query == RangeQuery{"academic_record", "grade", 6, 8}, "C4 <min> <max>");
        check(parse({"C5", "20221", "20222"}).query == RangeQuery{"academic_record", "term_code", 20221, 20222},
              "C5 <periodo1> <periodo2>");
        check(parse({"C6", "20241", "20242"}).query == RangeQuery{"student", "admission_term", 20241, 20242},
              "C6 <periodo1> <periodo2>");
        const query::ParsedQuery r = parse({"range", "course", "credits_tenths", "-5", "90"});
        check(r.catalog_id.empty() && r.query == RangeQuery{"course", "credits_tenths", -5, 90},
              "range <tabla> <columna> <min> <max>, con un negativo");
        const std::int64_t mn = std::numeric_limits<std::int64_t>::min();
        const std::int64_t mx = std::numeric_limits<std::int64_t>::max();
        check(parse({"C4", "-9223372036854775808", "9223372036854775807"}).query ==
                  RangeQuery{"academic_record", "grade", mn, mx},
              "acepta los extremos de 64 bits");
        check(parse({"C4", "7", "7"}).query.is_equality(), "un rango de un solo valor es una igualdad");
    }

    section("Consultas mal escritas");
    check_throws<QueryError>([] { parse({}); }, "sin consulta");
    check_throws<QueryError>([] { parse({"C9", "1"}); }, "consulta que todavia no se verifica (C9)");
    check_throws<QueryError>([] { parse({"c4", "6", "8"}); }, "id en minusculas");
    check_throws<QueryError>([] { parse({"C4", "6"}); }, "falta un valor");
    check_throws<QueryError>([] { parse({"C1", "1", "2"}); }, "sobra un valor");
    check_throws<QueryError>([] { parse({"C4", "8", "6"}); }, "rango invertido");
    check_throws<QueryError>([] { parse({"C2", "12abc"}); }, "valor que no es numero");
    check_throws<QueryError>([] { parse({"C2", "007"}); }, "ceros a la izquierda");
    check_throws<QueryError>([] { parse({"C2", "+7"}); }, "signo +");
    check_throws<QueryError>([] { parse({"C2", "-0"}); }, "-0");
    check_throws<QueryError>([] { parse({"C2", ""}); }, "valor vacio");
    check_throws<QueryError>([] { parse({"C2", "9223372036854775808"}); }, "mayor que el maximo de 64 bits");
    check_throws<QueryError>([] { parse({"C2", "-9223372036854775809"}); }, "menor que el minimo de 64 bits");
    check_throws<QueryError>([] { parse({"range", "student;", "x", "1", "2"}); }, "nombre de tabla invalido");
    check_throws<QueryError>([] { parse({"range", "student", "id", "1"}); }, "range incompleto");

    section("Texto SQL");
    check(query::to_sql(parse({"C1", "2024630001"}).query) ==
              "SELECT * FROM student WHERE enrollment_number = 2024630001",
          "igualdad");
    check(query::to_sql(parse({"C4", "6", "8"}).query) == "SELECT * FROM academic_record WHERE grade BETWEEN 6 AND 8",
          "rango");

    section("AIT de cada consulta");
    const scheme::KeyPair keys = scheme::init(32);
    const scheme::DatabaseDigest queries = digest_for(scheme::Preset::kQueries, keys);
    {
        bool same_as_catalog = true;
        for (const char* id : {"C1", "C2", "C3", "C4", "C5", "C6"}) {
            const std::vector<std::string> words =
                std::string(id) <= "C3" ? std::vector<std::string>{id, "1"} : std::vector<std::string>{id, "1", "2"};
            const scheme::TreeSpec spec = query::selection_tree(parse(words).query, queries);
            for (const scheme::CatalogQuery& c : scheme::case_study_catalog()) {
                if (c.id == id && !(c.trees.size() == 1 && c.trees[0] == spec)) same_as_catalog = false;
            }
        }
        check(same_as_catalog, "C1 a C6 usan exactamente el AIT que les asigna el catalogo");
    }
    check_throws<UnsupportedQuery>(
        [&] { query::selection_tree(RangeQuery{"student", "program_id", 1, 2}, queries); },
        "un rango sin AIT en el plan 'queries' no se puede verificar");
    {
        const scheme::DatabaseDigest full = digest_for(scheme::Preset::kFull, keys);
        check(query::selection_tree(RangeQuery{"student", "program_id", 1, 2}, full) ==
                  scheme::TreeSpec{"student", "program_id", "student_id"},
              "con el plan 'full' el mismo rango si tiene AIT");
    }
    check_throws<UnsupportedQuery>([&] { query::selection_tree(RangeQuery{"teacher", "id", 1, 2}, queries); },
                                   "tabla inexistente");
    check_throws<UnsupportedQuery>([&] { query::selection_tree(RangeQuery{"student", "age", 1, 2}, queries); },
                                   "columna inexistente");
    check_throws<UnsupportedQuery>(
        [&] { query::selection_tree(RangeQuery{"student", "first_name", 1, 2}, queries); }, "columna de texto");

    return test::finish();
}

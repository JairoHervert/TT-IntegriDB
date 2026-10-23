// query_test.cpp
//
// Pruebas de la lectura de consultas (C1 a C7, C11 a C13 y "range"),
// de su texto SQL y de la eleccion del AIT para seleccion y rango.

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
        // Las consultas de agregacion deben conservar el predicado exacto.
        // C11 usa grade < 6 y C12 utiliza dos condiciones de igualdad.
        const auto c11 = parse({"C11", "1"});
        check(c11.count.has_value() && c11.count->first == RangeQuery{"academic_record", "student_id", 1, 1} &&
              c11.count->second == RangeQuery{"academic_record", "grade", std::numeric_limits<std::int64_t>::min(), 5},
              "C11 usa igualdad en alumno y rango menor de 6 con limite inferior int64");
        check(query::to_sql(*c11.count) ==
              "SELECT COUNT(*) FROM academic_record WHERE student_id = 1 AND grade < 6",
              "C11 produce SQL estricto menor que 6");
        const auto c12 = parse({"C12", "1", "1"});
        check(c12.count.has_value() && c12.count->first == RangeQuery{"academic_record", "student_id", 1, 1} &&
              c12.count->second == RangeQuery{"academic_record", "evaluation_type", 1, 1},
              "C12 acepta dos igualdades para COUNT");
        check(query::to_sql(*c12.count) ==
              "SELECT COUNT(*) FROM academic_record WHERE student_id = 1 AND evaluation_type = 1",
              "C12 produce el SQL del catalogo");
        // JOIN C8 emplea AIT (course_id, course_id) y no admite parametros.
        check(parse({"C8"}).join_courses && parse({"C8"}).catalog_id == "C8",
              "C8 habilita JOIN autentico academic_record/course");
        // JOIN C9 no recibe parametros; usa los dos AIT (program_id, program_id).
        check(parse({"C9"}).join_programs && parse({"C9"}).catalog_id == "C9",
              "C9 habilita JOIN autenticado sin parametros");
        // MIN se identifica por alumno; C7 expresa dos rangos distintos.
        const auto c13 = parse({"C13", "1"});
        check(c13.min_student && *c13.min_student == 1 && c13.catalog_id == "C13",
              "C13 acepta alumno y activa MIN autentico");
        const auto c7 = parse({"C7", "6", "8", "20221", "20222"});
        check(c7.multi.has_value() && c7.query == RangeQuery{"academic_record", "grade", 6, 8} &&
                  c7.multi->second == RangeQuery{"academic_record", "term_code", 20221, 20222},
              "C7 admite dos intervalos autenticados");
        check(query::to_sql(*c7.multi) ==
                  "SELECT * FROM academic_record WHERE grade BETWEEN 6 AND 8 AND term_code BETWEEN 20221 AND 20222",
              "C7 produce SQL canonico");
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
    // Los nuevos comandos tambien rechazan parametros faltantes o extras;
    // no se interpretan consultas aun no implementadas.
    check_throws<QueryError>([] { parse({"C11"}); }, "C11 incompleta");
    check_throws<QueryError>([] { parse({"C11", "1", "5"}); }, "C11 con parametros extras");
    check_throws<QueryError>([] { parse({"C12", "1"}); }, "C12 incompleta");
    check_throws<QueryError>([] { parse({"C13"}); }, "C13 incompleta");
    check_throws<QueryError>([] { parse({"C13", "1", "6"}); }, "C13 con parametros extras");
    check_throws<QueryError>([] { parse({"C8", "1"}); }, "C8 no acepta parametros");
    check_throws<QueryError>([] { parse({"C9", "1"}); }, "C9 no acepta parametros");
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

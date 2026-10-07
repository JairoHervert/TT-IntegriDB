// tree_plan_test.cpp
//
// Pruebas de los planes de arboles: el catalogo de consultas, el plan
// "queries" (solo lo que usa el catalogo) y el plan "full" (m^2 por tabla).

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace {

using tt::scheme::PlannedTree;
using tt::scheme::Preset;
using tt::scheme::TreeSpec;

/*
 * Extrae solo la identificacion de cada arbol de un plan.
 */
std::vector<TreeSpec> specs_of(const std::vector<PlannedTree>& plan) {
    std::vector<TreeSpec> specs;
    for (const PlannedTree& p : plan) specs.push_back(p.spec);
    return specs;
}

}  // namespace

/*
 * Pruebas de los planes: el catalogo tiene sus 14 consultas con arboles
 * validos; "queries" produce exactamente los 11 arboles esperados, cada uno
 * usado por al menos una consulta; "full" produce m^2 arboles por tabla con
 * solo columnas enteras y contiene a "queries"; y los planes invalidos se
 * rechazan.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    using tt::test::check;
    using tt::test::check_throws;
    using tt::test::section;
    const std::vector<tt::db::TableSchema> schemas = tt::test::case_study_schemas();

    section("Catalogo de consultas del caso de estudio");
    {
        const auto& catalog = tt::scheme::case_study_catalog();
        std::set<std::string> ids;
        bool non_empty = true;
        for (const auto& q : catalog) {
            ids.insert(q.id);
            non_empty = non_empty && !q.trees.empty() && !q.sql.empty() && !q.description.empty() && !q.how.empty();
        }
        check(catalog.size() == 14 && ids.size() == 14, "14 consultas con ids distintos (C1 a C14)");
        check(non_empty, "cada consulta tiene descripcion, SQL, arboles y forma de verificarse");
        std::vector<TreeSpec> all;
        for (const auto& q : catalog) all.insert(all.end(), q.trees.begin(), q.trees.end());
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        tt::scheme::validate_plan(all, schemas);
        check(true, "todos sus arboles existen en el esquema y usan columnas enteras");
    }

    section("Plan queries");
    {
        const std::vector<PlannedTree> plan = tt::scheme::plan_trees(Preset::kQueries, schemas);
        const std::vector<TreeSpec> expected = {
            {"academic_record", "course_id", "course_id"},     {"academic_record", "evaluation_type", "record_id"},
            {"academic_record", "grade", "record_id"},         {"academic_record", "student_id", "grade"},
            {"academic_record", "student_id", "record_id"},    {"academic_record", "term_code", "record_id"},
            {"course", "course_id", "course_id"},              {"program", "program_id", "program_id"},
            {"student", "admission_term", "student_id"},
            {"student", "enrollment_number", "student_id"},    {"student", "program_id", "program_id"},
        };
        check(specs_of(plan) == expected, "son exactamente los 11 arboles esperados, en orden canonico");
        bool justified = std::all_of(plan.begin(), plan.end(), [](const PlannedTree& p) { return !p.used_by.empty(); });
        check(justified, "cada arbol lo usa al menos una consulta del catalogo");
        const auto grade = std::find_if(plan.begin(), plan.end(), [](const PlannedTree& p) {
            return p.spec == TreeSpec{"academic_record", "grade", "record_id"};
        });
        check(grade != plan.end() && grade->used_by == std::vector<std::string>{"C4", "C7", "C11", "C13", "C14"},
              "academic_record(grade -> record_id) lo usan C4, C7, C11, C13 y C14");
    }

    section("Plan full");
    {
        const std::vector<PlannedTree> plan = tt::scheme::plan_trees(Preset::kFull, schemas);
        check(plan.size() == 36 + 4 + 1 + 16, "57 arboles: 6^2 + 2^2 + 1^2 + 4^2 (solo columnas enteras)");
        const std::vector<TreeSpec> full = specs_of(plan);
        bool contains_queries = true;
        for (const PlannedTree& q : tt::scheme::plan_trees(Preset::kQueries, schemas)) {
            contains_queries = contains_queries && std::binary_search(full.begin(), full.end(), q.spec);
        }
        check(contains_queries, "full contiene todos los arboles de queries");
        const auto unused = std::count_if(plan.begin(), plan.end(), [](const PlannedTree& p) { return p.used_by.empty(); });
        check(unused == 57 - 11, "46 de los 57 arboles de full no los usa ninguna consulta del catalogo");
        bool no_text = std::none_of(full.begin(), full.end(), [](const TreeSpec& s) {
            return s.key_column == "course_code" || s.value_column == "first_name" || s.key_column == "program_name";
        });
        check(no_text, "ninguna columna de texto entra a un arbol");
    }

    section("Validacion de planes");
    {
        check(tt::scheme::parse_preset("queries") == Preset::kQueries && tt::scheme::parse_preset("full") == Preset::kFull &&
                  !tt::scheme::parse_preset("consultas"),
              "los planes se llaman 'queries' y 'full'");
        check_throws<tt::scheme::PlanError>(
            [&] { tt::scheme::validate_plan({{"course", "course_code", "course_id"}}, schemas); },
            "un arbol con columna de texto se rechaza");
        check_throws<tt::scheme::PlanError>(
            [&] { tt::scheme::validate_plan({{"teacher", "id", "id"}}, schemas); }, "una tabla que no existe se rechaza");
        check_throws<tt::scheme::PlanError>(
            [&] { tt::scheme::validate_plan({{"course", "course_id", "price"}}, schemas); },
            "una columna que no existe se rechaza");
        check_throws<tt::scheme::PlanError>(
            [&] {
                tt::scheme::validate_plan({{"course", "course_id", "course_id"}, {"course", "course_id", "course_id"}},
                                          schemas);
            },
            "un arbol repetido se rechaza");
        std::vector<tt::db::TableSchema> other = schemas;
        other[0].columns.pop_back();  // academic_record sin grade
        check_throws<tt::scheme::PlanError>([&] { tt::scheme::plan_trees(Preset::kQueries, other); },
                                            "queries sobre una base sin las columnas del caso de estudio se rechaza");
        check(tt::scheme::plan_trees(Preset::kFull, other).size() == 25 + 4 + 1 + 16, "full se adapta a cualquier esquema");
    }

    return tt::test::finish();
}

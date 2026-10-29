// service_fixture.hpp
//
// Estado autenticado de la base pequena del caso de estudio y un oraculo
// que calcula el resultado de cada consulta del catalogo recorriendo las
// filas directamente (sin AIT, sin ASO y sin las funciones del esquema).
// Lo usan service_test (servicios del nucleo) y net_test (red).

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "case_study_fixture.hpp"
#include "tt/db/row_store.hpp"
#include "tt/query/prover_service.hpp"
#include "tt/query/verifier_service.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace tt::test {

// Una consulta del catalogo escrita como en la linea de comandos.
struct CatalogCase {
    std::vector<std::string> words;
};

/*
 * Consultas que cubren las 14 formas del catalogo con parametros de la base
 * pequena, incluidos los casos NULL del alumno 8, que no tiene registros.
 */
inline std::vector<CatalogCase> catalog_cases() {
    return {
        {{"C1", "2024630001"}}, {{"C2", "1"}},           {{"C3", "1"}},
        {{"C4", "6", "8"}},     {{"C5", "20241", "20242"}}, {{"C6", "20241", "20242"}},
        {{"C7", "6", "8", "20241", "20242"}},              {{"C8"}},
        {{"C9"}},               {{"C10", "1"}},          {{"C11", "1"}},
        {{"C12", "1", "1"}},    {{"C13", "1"}},          {{"C14", "1"}},
        {{"C10", "8"}},         {{"C13", "8"}},          {{"C14", "8"}},
        {{"range", "academic_record", "evaluation_type", "2", "3"}},
    };
}

/*
 * Base, claves y estado autenticado (plan 'queries'), con el adaptador de
 * filas en memoria. Se construye una vez por prueba.
 */
struct ServiceFixture {
    db::Database base;
    scheme::KeyPair keys;
    scheme::SetupResult state;
    db::MemoryRowStore store;

    /*
     * Parametros:
     *   records  numero de registros academicos de la base pequena
     */
    explicit ServiceFixture(std::size_t records = 72)
        : base(case_study_database(records)), keys(scheme::init(160)), state(build(base, keys)), store(base) {}

    /*
     * Contexto del probador sobre este estado.
     */
    query::ProverContext context() {
        query::ProverContext c;
        c.database = &state.database;
        c.digest = &state.digest;
        c.public_key = &keys.pk.aso;
        c.store = &store;
        return c;
    }

    /*
     * Ejecuta Setup con el plan 'queries'.
     */
    static scheme::SetupResult build(const db::Database& d, const scheme::KeyPair& k) {
        std::vector<scheme::TreeSpec> specs;
        for (const auto& p : scheme::plan_trees(scheme::Preset::kQueries, case_study_schemas())) specs.push_back(p.spec);
        return scheme::setup(d, specs, "queries", k.sk, k.pk);
    }
};

// Resultado esperado de una consulta: filas (sin orden) o un valor.
struct Expected {
    bool scalar = false;
    std::vector<db::Row> rows;
    query::ResultValue value;
};

/*
 * Entero de una celda de la base pequena.
 */
inline std::int64_t at(const db::Row& r, std::size_t i) { return std::get<std::int64_t>(r[i]); }

/*
 * Filas de una tabla que cumplen un predicado.
 */
template <class Pred>
std::vector<db::Row> rows_where(const db::Database& d, const std::string& table, Pred pred) {
    std::vector<db::Row> out;
    for (const db::Row& r : d.find(table)->rows) {
        if (pred(r)) out.push_back(r);
    }
    return out;
}

/*
 * Oraculo: el resultado de cada consulta calculado recorriendo las tablas.
 * Indices de columnas segun case_study_schemas():
 *   academic_record: 0 record_id, 1 student_id, 2 course_id, 3 term_code,
 *                    4 evaluation_type, 5 grade
 *   course: 0 course_id, 3 credits_tenths;  student: 1 enrollment_number,
 *   5 program_id, 6 admission_term;  program: 0 program_id
 */
inline Expected oracle(const db::Database& d, const std::vector<std::string>& w) {
    Expected e;
    auto num = [&](std::size_t i) { return std::stoll(w[i]); };
    const std::string& id = w[0];
    if (id == "C1") {
        e.rows = rows_where(d, "student", [&](const db::Row& r) { return at(r, 1) == num(1); });
    } else if (id == "C2") {
        e.rows = rows_where(d, "academic_record", [&](const db::Row& r) { return at(r, 1) == num(1); });
    } else if (id == "C3") {
        e.rows = rows_where(d, "course", [&](const db::Row& r) { return at(r, 0) == num(1); });
    } else if (id == "C4") {
        e.rows = rows_where(d, "academic_record", [&](const db::Row& r) { return at(r, 5) >= num(1) && at(r, 5) <= num(2); });
    } else if (id == "C5") {
        e.rows = rows_where(d, "academic_record", [&](const db::Row& r) { return at(r, 3) >= num(1) && at(r, 3) <= num(2); });
    } else if (id == "C6") {
        e.rows = rows_where(d, "student", [&](const db::Row& r) { return at(r, 6) >= num(1) && at(r, 6) <= num(2); });
    } else if (id == "C7") {
        e.rows = rows_where(d, "academic_record", [&](const db::Row& r) {
            return at(r, 5) >= num(1) && at(r, 5) <= num(2) && at(r, 3) >= num(3) && at(r, 3) <= num(4);
        });
    } else if (id == "C8" || id == "C9") {
        const bool c8 = id == "C8";
        const auto& left = d.find(c8 ? "academic_record" : "student")->rows;
        const auto& right = d.find(c8 ? "course" : "program")->rows;
        const std::size_t key = c8 ? 2 : 5;
        for (const db::Row& l : left) {
            for (const db::Row& r : right) {
                if (at(r, 0) != at(l, key)) continue;
                db::Row joined = l;
                joined.insert(joined.end(), r.begin(), r.end());
                e.rows.push_back(joined);
            }
        }
    } else if (id == "range") {
        // Solo se usa con academic_record.evaluation_type.
        e.rows = rows_where(d, "academic_record", [&](const db::Row& r) { return at(r, 4) >= num(3) && at(r, 4) <= num(4); });
    } else {
        e.scalar = true;
        const auto mine = rows_where(d, "academic_record", [&](const db::Row& r) { return at(r, 1) == num(1); });
        if (id == "C10") {
            std::int64_t sum = 0;
            for (const db::Row& r : mine) sum += at(r, 5);
            e.value = mine.empty() ? query::ResultValue{} : query::ResultValue{static_cast<double>(sum) / mine.size()};
        } else if (id == "C11" || id == "C12") {
            std::int64_t count = 0;
            for (const db::Row& r : mine) count += id == "C11" ? at(r, 5) < 6 : at(r, 4) == num(2);
            e.value = count;
        } else if (id == "C13") {
            std::optional<std::int64_t> min;
            for (const db::Row& r : mine) min = min ? std::min(*min, at(r, 5)) : at(r, 5);
            e.value = min ? query::ResultValue{*min} : query::ResultValue{};
        } else if (id == "C14") {
            std::unordered_map<std::int64_t, std::int64_t> credits;
            for (const db::Row& c : d.find("course")->rows) credits[at(c, 0)] = at(c, 3);
            std::int64_t sum = 0;
            bool any = false;
            for (const db::Row& r : mine) {
                if (at(r, 5) < 6 || !credits.count(at(r, 2))) continue;
                sum += credits[at(r, 2)];
                any = true;
            }
            e.value = any ? query::ResultValue{sum} : query::ResultValue{};
        }
    }
    return e;
}

/*
 * Convierte las filas de un resultado verificado en filas de la base, para
 * compararlas con el oraculo.
 */
inline std::vector<db::Row> as_rows(const query::ResultTable& t) {
    std::vector<db::Row> out;
    for (const auto& row : t.rows) {
        db::Row r;
        for (const query::ResultValue& v : row) {
            if (const auto* i = std::get_if<std::int64_t>(&v)) {
                r.emplace_back(std::in_place_index<0>, *i);
            } else {
                const auto* s = std::get_if<std::string>(&v);
                r.emplace_back(std::in_place_index<1>, s != nullptr ? *s : std::string("<no es entero ni texto>"));
            }
        }
        out.push_back(std::move(r));
    }
    return out;
}

/*
 * Indica si un resultado verificado coincide con el esperado: las mismas
 * filas (sin importar el orden) o el mismo valor.
 */
inline bool matches(const query::ResultTable& got, const Expected& expected) {
    if (expected.scalar) {
        return got.rows.size() == 1 && got.rows[0].size() == 1 && got.rows[0][0] == expected.value;
    }
    std::vector<db::Row> a = as_rows(got);
    std::vector<db::Row> b = expected.rows;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

}  // namespace tt::test

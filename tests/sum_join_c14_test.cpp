// C14: composicion criptografica C7 + JOIN filtrado + SUM del Verifier.
// La prueba usa datos en memoria y contrasta contra un oraculo SQL independiente.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/query/sum_join_course.hpp"
#include "tt/query/response_io.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

/*
 * Calcula un oraculo de SUM sobre JOIN y WHERE usando la base en memoria.
 * No consulta AIT ni reutiliza las funciones ProofGen o Verify de C14.
 *
 * Parametros:
 *   base     tablas originales del caso de prueba
 *   student  alumno seleccionado por la clausula WHERE
 *   count    contador opcional de registros que participan en el JOIN
 *
 * Devuelve:
 *   Suma en decimas o NULL si ninguna fila participa en el INNER JOIN.
 */
static std::optional<std::int64_t> oracle(const db::Database& base, std::int64_t student,
                                           std::size_t* count = nullptr) {
    std::unordered_map<std::int64_t, std::int64_t> credits;
    for (const auto& course : base.find("course")->rows)
        credits.emplace(std::get<std::int64_t>(course[0]), std::get<std::int64_t>(course[3]));
    std::int64_t sum = 0;
    std::size_t joined = 0;
    for (const auto& record : base.find("academic_record")->rows) {
        if (std::get<std::int64_t>(record[1]) != student ||
            std::get<std::int64_t>(record[5]) < 6) continue;
        const auto found = credits.find(std::get<std::int64_t>(record[2]));
        if (found == credits.end()) continue;
        sum += found->second;
        ++joined;
    }
    if (count) *count = joined;
    if (!joined) return std::nullopt;
    return sum;
}

/*
 * Ejecuta las pruebas de C14 sobre bases sinteticas en memoria.
 * El Verifier recibe unicamente digest, pk y respuesta del Prover.
 *
 * Casos:
 *   Resultado SQL, multiplicidades, SUM NULL/0, rango vacio,
 *   omisiones, AIT/ASO/Bezout/Merkle alterados y serializacion TTRSP.
 *
 * Devuelve:
 *   Cero si todas las comprobaciones pasan; uno cuando hay fallas,
 *   mediante test::finish() como en las pruebas de C8/C9.
 */
int main() {
    const auto keys = scheme::init(160);
    std::vector<scheme::TreeSpec> specs;
    for (const auto& plan : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas()))
        specs.push_back(plan.spec);
    const auto base = test::case_study_database(72);
    const auto state = scheme::setup(base, specs, "queries", keys.sk, keys.pk);
    db::MemoryRowStore store(base);
    const std::int64_t student = 1;
    const auto honest = query::answer_sum_join_course(state.database, state.digest,
                                                       keys.pk.aso, student, store).response;
    const auto verify = [&](const query::SumJoinCourseResponse& resp) {
        return query::verify_sum_join_course(state.digest, keys.pk.aso, student, resp);
    };

    section("C14: resultado SQL con multiplicidades y pruebas completas");
    const auto valid = verify(honest);
    std::size_t expected_count = 0;
    const auto expected = oracle(base, student, &expected_count);
    check(valid.accepted && valid.credits_tenths == expected && valid.joined_rows == expected_count,
          "SUM de C14 coincide exactamente con el JOIN + WHERE de SQL");
    check(honest.filtered.query == query::c14_filter(student),
          "la prueba C7 usa student_id = ? AND grade >= 6");
    check(!honest.filtered.ranges[0].entries.empty() && !honest.course_range.entries.empty(),
          "el resultado contiene RangeCover autenticos de ambos pasos");

    section("C14: formato TTRSP tipo 7 independiente de C7/C8/C9");
    const auto bytes = query::encode_sum_join_course_response(honest);
    const auto decoded = query::decode_sum_join_course_response(bytes);
    check(verify(decoded).accepted && query::encode_sum_join_course_response(decoded) == bytes,
          "round-trip conserva las pruebas criptograficas");
    check_throws<query::ResponseFormatError>([&] { (void)query::decode_join_course_response(bytes); },
                                             "C8 no interpreta C14 como C8");
    check_throws<query::ResponseFormatError>([&] { (void)query::decode_multirange_response(bytes); },
                                             "C7 no interpreta C14 como C7");
    {
        auto changed = bytes; changed.push_back(0);
        check_throws<query::ResponseFormatError>([&] { (void)query::decode_sum_join_course_response(changed); },
                                                 "rechaza bytes sobrantes");
    }
    {
        auto changed = bytes; changed.pop_back();
        check_throws<query::ResponseFormatError>([&] { (void)query::decode_sum_join_course_response(changed); },
                                                 "rechaza respuesta truncada");
    }

    section("C14: ataques a filtros, AIT, ASO, Merkle y consulta");
    {
        auto bad = honest; bad.student_id = 2;
        check(!verify(bad).accepted, "rechaza consulta para alumno distinto");
    }
    {
        auto bad = honest; bad.state_id[0] ^= 1;
        check(!verify(bad).accepted, "rechaza digest incorrecto");
    }
    {
        auto bad = honest; bad.filtered.rows.pop_back();
        check(!verify(bad).accepted, "C7 no permite omitir registros aprobados");
    }
    {
        auto bad = honest; bad.filtered.rows.push_back(bad.filtered.rows.front());
        check(!verify(bad).accepted, "C7 rechaza duplicacion de registro");
    }
    {
        auto bad = honest; bad.filtered.rows.front().row[5] = test::int_cell(3);
        check(!verify(bad).accepted, "C7 rechaza fila fuera del filtro y Merkle invalido");
    }
    {
        auto bad = honest; bad.course_range.entries.clear();
        check(!verify(bad).accepted, "no se acepta ausencia de AIT course");
    }
    {
        auto bad = honest; bad.course_complement.rest_g1.clear();
        check(!verify(bad).accepted, "se rechaza Split ASO falso");
    }
    {
        auto bad = honest; bad.no_missing_courses.bezout_u.clear();
        check(!verify(bad).accepted, "se rechaza Bezout falso");
    }
    if (!honest.course_rows.empty()) {
        auto bad = honest; bad.course_rows.pop_back();
        check(!verify(bad).accepted, "se rechaza omitir curso que coincide con C7");
        bad = honest; bad.course_rows.push_back(bad.course_rows.front());
        check(!verify(bad).accepted, "se rechaza course_id duplicado");
        bad = honest; bad.course_rows.front().row[3] = test::int_cell(9999);
        check(!verify(bad).accepted, "se rechaza alterar creditos (Merkle)");
        bad = honest; bad.course_rows.front().row[0] = test::int_cell(9999);
        check(!verify(bad).accepted, "se rechaza sustituir course_id");
    }

    section("C14: multiplicidades explicitas y SUM 0 distinto de NULL");
    {
        auto repeated = base;
        // Dos filas aprobadas del mismo alumno y materia: SQL SUM cuenta dos.
        repeated.tables[0].rows.clear();
        repeated.tables[0].rows.push_back({test::int_cell(1), test::int_cell(1), test::int_cell(2),
                                           test::int_cell(20241), test::int_cell(1), test::int_cell(6)});
        repeated.tables[0].rows.push_back({test::int_cell(2), test::int_cell(1), test::int_cell(2),
                                           test::int_cell(20242), test::int_cell(1), test::int_cell(10)});
        repeated.tables[0].rows.push_back({test::int_cell(3), test::int_cell(1), test::int_cell(2),
                                           test::int_cell(20242), test::int_cell(1), test::int_cell(5)});
        const auto st = scheme::setup(repeated, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(repeated);
        const auto reply = query::answer_sum_join_course(st.database, st.digest,
                                                          keys.pk.aso, student, source).response;
        const auto checked = query::verify_sum_join_course(st.digest, keys.pk.aso, student, reply);
        check(checked.accepted && checked.joined_rows == 2 &&
              checked.credits_tenths == std::optional<std::int64_t>{180} &&
              checked.course_rows == 1,
              "2 registros aprobados con misma materia suman 2 veces los 90 decimos");
        // En SQL, SUM sobre filas con valores cero es 0, no NULL.
        repeated.tables[1].rows[1][3] = test::int_cell(0);
        const auto zero_state = scheme::setup(repeated, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore zero_source(repeated);
        const auto zero = query::answer_sum_join_course(zero_state.database, zero_state.digest,
                                                         keys.pk.aso, student, zero_source).response;
        const auto zero_checked = query::verify_sum_join_course(zero_state.digest, keys.pk.aso, student, zero);
        check(zero_checked.accepted && zero_checked.joined_rows == 2 &&
              zero_checked.credits_tenths == std::optional<std::int64_t>{0},
              "SUM = 0 con filas JOIN no es NULL");
    }

    section("C14: JOIN parcial y resultados vacios SQL NULL");
    {
        auto partial = base;
        // El registro aprobado del alumno 1 puede referir a una materia
        // que no exista: INNER JOIN lo excluye y Bezout prueba la ausencia.
        for (auto& row : partial.tables[0].rows) {
            if (std::get<std::int64_t>(row[1]) == student &&
                std::get<std::int64_t>(row[5]) >= 6) {
                row[2] = test::int_cell(999);
                break;
            }
        }
        const auto st = scheme::setup(partial, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(partial);
        const auto reply = query::answer_sum_join_course(st.database, st.digest,
                                                         keys.pk.aso, student, source).response;
        const auto checked = query::verify_sum_join_course(st.digest, keys.pk.aso, student, reply);
        check(checked.accepted && checked.credits_tenths == oracle(partial, student),
              "registro sin materia no suma; se verifica ausencia en el JOIN");
    }
    {
        const auto reply = query::answer_sum_join_course(state.database, state.digest,
                                                           keys.pk.aso, 999, store).response;
        const auto checked = query::verify_sum_join_course(state.digest, keys.pk.aso, 999, reply);
        check(checked.accepted && !checked.credits_tenths && checked.joined_rows == 0,
              "alumno sin registros aprobados: SUM = NULL, no 0");
    }
    {
        auto partial = base;
        for (auto& row : partial.tables[0].rows) {
            if (std::get<std::int64_t>(row[1]) == student)
                row[5] = test::int_cell(5);
        }
        const auto st = scheme::setup(partial, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(partial);
        const auto reply = query::answer_sum_join_course(st.database, st.digest,
                                                          keys.pk.aso, student, source).response;
        const auto checked = query::verify_sum_join_course(st.digest, keys.pk.aso, student, reply);
        check(checked.accepted && !checked.credits_tenths && checked.joined_rows == 0,
              "sin calificaciones aprobatorias: SUM = NULL");
    }
    {
        auto partial = base;
        partial.tables[1].rows.clear();
        const auto st = scheme::setup(partial, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(partial);
        const auto reply = query::answer_sum_join_course(st.database, st.digest,
                                                          keys.pk.aso, student, source).response;
        const auto checked = query::verify_sum_join_course(st.digest, keys.pk.aso, student, reply);
        check(checked.accepted && !checked.credits_tenths && checked.joined_rows == 0,
              "course vacia: INNER JOIN vacio, SUM NULL");
    }
    return test::finish();
}

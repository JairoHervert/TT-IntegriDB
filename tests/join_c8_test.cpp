// C8: JOIN autentico academic_record.course_id = course.course_id.
// Verifica resultados honestos, multiplicidades, completitud ASO, Merkle,
// serializacion independiente de C9 y ataques que alteran (R, pi).

#include <algorithm>
#include <cstdint>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/query/join_course.hpp"
#include "tt/query/join_program.hpp"
#include "tt/query/response_io.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

/*
 * Ejecuta la regresion de C8 exclusivamente sobre una base en memoria.
 * El Verifier usa pk/delta/respuesta, nunca la base ni la clave privada.
 *
 * Casos:
 *   Multiplicidad de course_id, curso sin registros, registro sin curso,
 *   resultado vacio, tampering Merkle/ASO, omisiones y respuesta binaria.
 *
 * Devuelve:
 *   Cero cuando todas las comprobaciones pasan; uno si alguna falla.
 */
int main() {
    const auto specs = [] {
        std::vector<scheme::TreeSpec> out;
        for (const auto& planned : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas()))
            out.push_back(planned.spec);
        return out;
    }();
    const auto keys = scheme::init(160);
    const auto base = test::case_study_database(45);
    const auto state = scheme::setup(base, specs, "queries", keys.sk, keys.pk);
    db::MemoryRowStore store(base);
    const auto honest = query::answer_join_course(state.database, state.digest, keys.pk.aso, store).response;
    const auto verify = [&](const query::JoinCourseResponse& response) {
        return query::verify_join_course(state.digest, keys.pk.aso, response);
    };

    section("C8: resultados autenticos y multiplicidades de course_id");
    const auto valid = verify(honest);
    check(valid.accepted && valid.joined_rows == 45 && valid.course_rows.size() == 6,
          "los 45 registros se unen con sus cursos; course_id puede repetirse");
    check(honest.record_rows.size() == 45 && honest.course_rows.size() == 6,
          "las filas de ambas tablas incluyen sus respectivas pruebas Merkle");
    // Oraculo funcional separado de los certificados: las 45 filas originales
    // aparecen exactamente una vez, aunque course_id se repita.
    bool matches_sql = valid.accepted && valid.record_rows.size() == base.tables[0].rows.size();
    for (const auto& expected : base.tables[0].rows)
        matches_sql = matches_sql &&
                      std::count(valid.record_rows.begin(), valid.record_rows.end(), expected) == 1;
    check(matches_sql, "el resultado coincide con JOIN SQL sobre la base original");

    section("C8: formato TTRSP tipo 6, independiente del formato C9");
    const auto bytes = query::encode_join_course_response(honest);
    const auto decoded = query::decode_join_course_response(bytes);
    check(verify(decoded).accepted, "serializar y deserializar conserva todas las pruebas");
    check_throws<query::ResponseFormatError>([&] {
        (void)query::decode_join_program_response(bytes);
    }, "C9 no acepta por error los bytes de C8");
    {
        auto broken = bytes;
        broken.push_back(0);
        check_throws<query::ResponseFormatError>([&] {
            (void)query::decode_join_course_response(broken);
        }, "se rechazan bytes sobrantes");
    }
    {
        auto broken = bytes;
        broken.pop_back();
        check_throws<query::ResponseFormatError>([&] {
            (void)query::decode_join_course_response(broken);
        }, "se rechaza un mensaje truncado");
    }

    section("C8: se rechazan omisiones y manipulaciones");
    {
        auto bad = honest;
        bad.record_rows.pop_back();
        check(!verify(bad).accepted, "omitir un registro academico coincidente");
    }
    {
        auto bad = honest;
        bad.course_rows.pop_back();
        check(!verify(bad).accepted, "omitir un curso presente en el JOIN");
    }
    {
        auto bad = honest;
        bad.record_rows.push_back(bad.record_rows.front());
        check(!verify(bad).accepted, "duplicar record_id invalida la respuesta");
    }
    {
        auto bad = honest;
        std::get<std::int64_t>(bad.record_rows.front().row[5]) += 1;
        check(!verify(bad).accepted, "alterar una calificacion invalida Merkle");
    }
    {
        auto bad = honest;
        std::get<std::string>(bad.course_rows.front().row[2]) = "CAMBIADO";
        check(!verify(bad).accepted, "alterar el curso invalida Merkle");
    }
    {
        auto bad = honest;
        bad.splits[0].rest_g1.clear();
        check(!verify(bad).accepted, "testigo Split ASO falso");
    }
    {
        auto bad = honest;
        bad.disjoint[1].bezout_u.clear();
        check(!verify(bad).accepted, "certificado Bezout falso");
    }
    {
        auto bad = honest;
        bad.ranges[1].entries.clear();
        check(!verify(bad).accepted, "AIT de course omitido");
    }
    {
        auto bad = honest;
        bad.state_id[0] ^= 1;
        check(!verify(bad).accepted, "digest de estado incorrecto");
    }

    section("C8: INNER JOIN parcial y conjuntos vacios");
    {
        auto partial = base;
        // El registro 45 hace referencia a una clave ausente de course.
        std::get<std::int64_t>(partial.tables[0].rows.back()[2]) = 999;
        const auto st = scheme::setup(partial, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(partial);
        const auto answer = query::answer_join_course(st.database, st.digest, keys.pk.aso, source).response;
        const auto checked = query::verify_join_course(st.digest, keys.pk.aso, answer);
        check(checked.accepted && checked.joined_rows == 44,
              "registro sin curso no pertenece al JOIN y queda certificado por Bezout");
    }
    {
        auto partial = base;
        // Curso adicional sin registros: prueba de completitud bidireccional.
        partial.tables[1].rows.push_back({test::int_cell(99), test::text_cell("NEW"),
                                          test::text_cell("Sin registros"), test::int_cell(50)});
        const auto st = scheme::setup(partial, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(partial);
        const auto answer = query::answer_join_course(st.database, st.digest, keys.pk.aso, source).response;
        const auto checked = query::verify_join_course(st.digest, keys.pk.aso, answer);
        check(checked.accepted && checked.joined_rows == 45 && checked.course_rows.size() == 6,
              "curso no referenciado no se incluye en el JOIN");
    }
    {
        auto empty = base;
        empty.tables[0].rows.clear();
        const auto st = scheme::setup(empty, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(empty);
        const auto answer = query::answer_join_course(st.database, st.digest, keys.pk.aso, source).response;
        const auto checked = query::verify_join_course(st.digest, keys.pk.aso, answer);
        check(checked.accepted && checked.joined_rows == 0 && checked.course_rows.empty(),
              "si no existen registros academicos, el JOIN vacio se verifica");
    }
    return test::finish();
}

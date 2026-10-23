// min_c13_test.cpp
//
// C13 reduce MIN a un rango autenticado, siguiendo IntegriDB 4.4.
// La prueba debe demostrar que existe un minimo y no hay otro menor.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/query/minimum.hpp"
#include "tt/query/response_io.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

/*
 * Comprueba la consulta MIN(grade) por alumno sobre el estado autenticado.
 *
 * Casos:
 *   Minimos correctos, existencia de testigos, resultado NULL,
 *   reutilizacion del formato de C7 y manipulaciones del rango.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    // Setup y MemoryRowStore simulan los actores sobre datos en memoria.
    // No se depende de la conexion real con MySQL en este test.
    const auto base = test::case_study_database(120);
    const auto keys = scheme::init(160);
    std::vector<scheme::TreeSpec> specs;
    for (const auto& t : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas()))
        specs.push_back(t.spec);
    const auto state = scheme::setup(base, specs, "queries", keys.sk, keys.pk);
    db::MemoryRowStore store(base);
    const auto prove = [&](std::int64_t id) {
        return query::answer_minimum(state.database, state.digest, keys.pk.aso, id, store).range_answer.response;
    };
    const auto verify = [&](std::int64_t id, const query::MultiRangeResponse& r) {
        return query::verify_minimum(state.digest, keys.pk.aso, id, r);
    };
    // Oraculo independiente: recorre todas las filas del alumno y obtiene
    // su menor calificacion sin utilizar el AIT ni el algoritmo MIN.
    const auto expected_min = [&](std::int64_t id) -> std::optional<std::int64_t> {
        std::optional<std::int64_t> min;
        for (const auto& row : base.find("academic_record")->rows) {
            if (std::get<std::int64_t>(row[1]) != id) continue;
            const auto grade = std::get<std::int64_t>(row[5]);
            if (!min || grade < *min) min = grade;
        }
        return min;
    };

    // Casos positivos: el minimo autenticado debe coincidir con el calculo
    // directo y al menos una fila debe atestiguar su existencia.
    section("C13 verifica minimo del alumno con un RangeCover multidimensional");
    for (std::int64_t id = 1; id <= 7; ++id) {
        const auto r = prove(id);
        const auto v = verify(id, r);
        check(v.accepted && v.minimum == expected_min(id),
              "alumno " + std::to_string(id) + ": MIN coincide con base autenticada");
        check(v.witnesses_rows > 0, "se demuestra existencia de al menos una fila minima");
    }

    // Sin filas, SQL define MIN como NULL; esa ausencia tambien se autentica.
    // Una prueba vacia de otro alumno no se puede reutilizar.
    section("SQL NULL cuando el alumno no tiene registros");
    const auto empty = prove(8);
    const auto nullable = verify(8, empty);
    check(nullable.accepted && !nullable.minimum && nullable.witnesses_rows == 0,
          "alumno 8: se autentica ausencia de registros y MIN = NULL");
    check(!verify(1, empty).accepted, "respuesta NULL no sirve para un alumno que tiene registros");

    // C13 reutiliza la respuesta multidimensional de C7, sin tipo nuevo.
    section("La misma serializacion TTRSP tipo 2 de C7 sirve para MIN");
    const auto honest = prove(1);
    const auto bytes = query::encode_multirange_response(honest);
    check(verify(1, query::decode_multirange_response(bytes)).accepted,
          "C13 soporta ida y vuelta binaria sin formato nuevo");

    // Cambiar el minimo altera el rango certificado. Omitir o modificar
    // las filas rompe completitud o la autenticacion mediante Merkle.
    section("Ataques contra minimo y completitud");
    {
        auto wrong = honest;
        // Falso minimo mayor: incluye un intervalo no probado por el AIT.
        wrong.query.second.hi += 1;
        check(!verify(1, wrong).accepted,
              "si se aumenta el minimo sin modificar la evidencia, falla el RangeCover");
    }
    {
        auto wrong = honest;
        // Falso minimo menor: tampoco corresponde al rango autenticado.
        wrong.query.second.hi -= 1;
        check(!verify(1, wrong).accepted, "si se reduce el minimo, la prueba no corresponde al rango");
    }
    {
        auto wrong = honest;
        // Se oculta una fila minima aun cuando se conserve la prueba ASO.
        wrong.rows.pop_back();
        check(!verify(1, wrong).accepted, "omitir la fila minima impide completar la interseccion");
    }
    {
        auto wrong = honest;
        wrong.rows.push_back(honest.rows.front());
        check(!verify(1, wrong).accepted, "fila minima duplicada rechazada");
    }
    {
        auto wrong = honest;
        std::get<std::int64_t>(wrong.rows[0].row[5]) += 1;
        check(!verify(1, wrong).accepted, "calificacion de fila adulterada rechazada");
    }
    {
        auto wrong = honest;
        wrong.state_id[0] ^= 1;
        check(!verify(1, wrong).accepted, "respuesta ligada a digest incorrecto rechazada");
    }
    {
        auto wrong = honest;
        wrong.query.first.lo = 2;
        wrong.query.first.hi = 2;
        check(!verify(1, wrong).accepted, "no se acepta prueba de otro alumno");
    }
    {
        auto wrong = empty;
        wrong.query.second.hi = 5;
        check(!verify(8, wrong).accepted, "cambio de rango vacio no autentico");
    }
    return test::finish();
}

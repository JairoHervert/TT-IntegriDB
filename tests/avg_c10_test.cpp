// avg_c10_test.cpp
//
// C10 comprueba AVG mediante SUM autentica y COUNT del AIT existente.
// Incluye calificaciones repetidas, NULL y manipulaciones de testigos ASO.

#include <cstdint>
#include <optional>
#include <utility>
#include <stdexcept>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/query/average.hpp"
#include "tt/query/response_io.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

/*
 * Comprueba AVG(grade) por alumno con el mecanismo SUM de IntegriDB.
 *
 * Casos:
 *   SUM y COUNT independientes, multiplicidad, AVG NULL, serializacion,
 *   coeficientes falsos, testigos bilineales y digest incorrecto.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    // La base se autentica en memoria para aislar el protocolo ASO.
    // Los resultados SQL se contrastan con un recorrido directo de filas.
    const auto base = test::case_study_database(120);
    const auto keys = scheme::init(160);
    std::vector<scheme::TreeSpec> specs;
    for (const auto& t : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas()))
        specs.push_back(t.spec);
    const auto state = scheme::setup(base, specs, "queries", keys.sk, keys.pk);
    const auto prove = [&](std::int64_t id) {
        return query::answer_average(state.database, state.digest, keys.pk.aso, id);
    };
    const auto verify = [&](std::int64_t id, const query::AverageResponse& r) {
        return query::verify_average(state.digest, keys.pk.aso, id, r);
    };
    // Oraculo independiente: suma todas las calificaciones del alumno,
    // incluidas las repetidas, y cuenta sus filas sin utilizar los AIT.
    const auto expected = [&](std::int64_t id) {
        std::int64_t sum = 0;
        std::uint64_t count = 0;
        for (const auto& row : base.find("academic_record")->rows) {
            if (std::get<std::int64_t>(row[1]) == id) {
                sum += std::get<std::int64_t>(row[5]);
                ++count;
            }
        }
        return std::make_pair(sum, count);
    };
    // La prueba debe aceptar la suma y la cardinalidad exactas; el AVG
    // se deriva de ambos valores en el verificador, sin confiar en el Prover.
    section("C10: SUM y COUNT del AIT, incluyendo calificaciones repetidas");
    for (std::int64_t id = 1; id <= 7; ++id) {
        const auto answer = prove(id);
        const auto checked = verify(id, answer);
        const auto [sum, count] = expected(id);
        check(checked.accepted && checked.sum == sum && checked.count == count,
              "alumno " + std::to_string(id) + ": SUM, COUNT y AVG equivalentes a SQL");
    }
    // Si COUNT=0, el resultado de AVG es NULL y no se divide entre cero.
    section("C10: AVG SQL NULL si no hay filas");
    const auto empty = prove(8);
    check(verify(8, empty).accepted && empty.count == 0,
          "alumno sin filas: COUNT=0 y AVG=NULL");
    check(!verify(1, empty).accepted, "no se reutiliza respuesta vacia para otro alumno");

    // El tipo 4 se decodifica y verifica. Cada mutacion posterior debe ser
    // detectada sin necesidad de consultar MySQL durante la verificacion.
    section("C10: serializacion TTRSP tipo 4 y ataques");
    const auto honest = prove(1);
    const auto bytes = query::encode_average_response(honest);
    check(verify(1, query::decode_average_response(bytes)).accepted,
          "ida y vuelta binaria de testigos");
    {
        auto bad = honest;
        // Cambiar SUM o COUNT debe invalidar los testigos autenticados.
        bad.sum += 1;
        check(!verify(1, bad).accepted, "suma falsa rechazada");
    }
    {
        auto bad = honest;
        bad.count += 1;
        check(!verify(1, bad).accepted, "contador falso rechazado");
    }
    {
        auto bad = honest;
        // Los coeficientes del polinomio estan ligados al acumulador del AIT.
        bad.proof.a0[31] ^= 1;
        check(!verify(1, bad).accepted, "coeficiente constante falsificado rechazado");
    }
    {
        auto bad = honest;
        bad.proof.a1[31] ^= 1;
        check(!verify(1, bad).accepted, "coeficiente lineal falsificado rechazado");
    }
    {
        auto bad = honest;
        // Los testigos w1 y w2 deben satisfacer las ecuaciones bilineales.
        bad.proof.w1.clear();
        check(!verify(1, bad).accepted, "testigo w1 alterado rechazado");
    }
    {
        auto bad = honest;
        bad.proof.w2.clear();
        check(!verify(1, bad).accepted, "testigo w2 alterado rechazado");
    }
    {
        auto bad = honest;
        bad.state_id[0] ^= 1;
        check(!verify(1, bad).accepted, "digest distinto rechazado");
    }
    {
        auto bad = honest;
        bad.student_id = 2;
        check(!verify(1, bad).accepted, "consulta por alumno distinto rechazada");
    }
    {
        auto bad = honest;
        // Un acumulador no es valido sin el RangeCover que lo autentica.
        bad.range.entries.clear();
        check(!verify(1, bad).accepted, "RangeCover ausente rechazado");
    }
    {
        auto bad = bytes;
        bad.push_back(0);
        check_throws<query::ResponseFormatError>([&]{ (void) query::decode_average_response(bad); },
                     "bytes adicionales rechazados");
    }
    return test::finish();
}

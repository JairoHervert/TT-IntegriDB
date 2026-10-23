// count_c11_test.cpp
//
// C11 cuenta evaluaciones no acreditadas (grade < 6) con AIT + ASO.
// Los casos usan D~ en memoria; no necesitan consultar MySQL.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/query/count_intersection.hpp"
#include "tt/query/query.hpp"
#include "tt/query/response_io.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

/*
 * Comprueba el COUNT autenticado de C11, distinto del predicado de C12.
 *
 * Casos:
 *   SQL y CLI, COUNT correcto, formato binario, predicados prohibidos,
 *   omisiones, testigos falsos y conteo cero.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    // Misma estructura de tablas, claves y arboles que el plan queries.
    const auto base = test::case_study_database(190);
    const auto keys = scheme::init(260);
    std::vector<scheme::TreeSpec> trees;
    for (const auto& plan : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas())) {
        trees.push_back(plan.spec);
    }
    const auto state = scheme::setup(base, trees, "queries", keys.sk, keys.pk);

    const auto make_query = [](std::int64_t student) {
        return query::CountQuery{
            "C11", {"academic_record", "student_id", student, student},
            {"academic_record", "grade", std::numeric_limits<std::int64_t>::min(), 5}};
    };
    // COUNT de referencia calculado recorriendo las filas sin usar AIT.
    // El corte grade < 6 se comprueba de forma independiente.
    const auto expected = [&](std::int64_t student) {
        std::uint64_t total = 0;
        for (const auto& row : base.find("academic_record")->rows) {
            if (std::get<std::int64_t>(row[1]) == student &&
                std::get<std::int64_t>(row[5]) < 6) ++total;
        }
        return total;
    };
    const auto prove = [&](const query::CountQuery& q) {
        return query::answer_count(state.database, state.digest, keys.pk.aso, q).response;
    };
    const auto verify = [&](const query::CountQuery& q, const query::CountResponse& r) {
        return query::verify_count(state.digest, keys.pk.aso, q, r);
    };

    // Caso positivo: el valor verificado debe coincidir con el recorrido
    // de referencia y la respuesta debe incluir testigos, no filas SQL.
    section("C11: COUNT autenticado con student_id = ? y grade < 6");
    const auto q = make_query(1);
    const auto honest = prove(q);
    const auto v = verify(q, honest);
    check(v.accepted && v.count == expected(1) && v.count > 0,
          "C11 coincide con COUNT independiente de los datos y verifica ASO");
    check(honest.references.size() == honest.count && !honest.splits[0].empty() &&
          !honest.splits[1].empty() && !honest.disjoint.empty(),
          "transfiere referencias y testigos ASO, no filas completas");
    check(query::to_sql(q) ==
          "SELECT COUNT(*) FROM academic_record WHERE student_id = 1 AND grade < 6",
          "SQL corresponde exactamente al catalogo C11");
    const auto parsed = query::parse_query({"C11", "1"});
    check(parsed.count.has_value() && *parsed.count == q,
          "CLI interpreta C11 como igualdad + rango abierto por arriba");

    // Aunque C11 y C12 usan el mismo formato, la prueba esta ligada a
    // la consulta exacta: no se acepta bajo otros filtros ni identificador.
    section("C11: respuesta tipo 3 y aislamiento de C12");
    const auto bytes = query::encode_count_response(honest);
    const auto decoded = query::decode_count_response(bytes);
    check(verify(q, decoded).accepted && query::encode_count_response(decoded) == bytes,
          "COUNT tipo 3 serializa y deserializa C11");
    auto truncated = bytes;
    truncated.pop_back();
    check_throws<query::ResponseFormatError>(
        [&] { query::decode_count_response(truncated); }, "rechaza formato truncado");
    const auto q12 = query::CountQuery{"C12", {"academic_record", "student_id", 1, 1},
                                          {"academic_record", "evaluation_type", 1, 1}};
    check(!verify(q12, honest).accepted, "C12 no acepta respuesta C11");
    // El contrato impide autenticar una condicion diferente bajo la etiqueta C11.
    auto altered_predicate = q;
    altered_predicate.second.hi = 6;
    check_throws<query::UnsupportedQuery>(
        [&] { query::answer_count(state.database, state.digest, keys.pk.aso, altered_predicate); },
        "C11 no permite reemplazar grade < 6 por grade <= 6");

    // Cada ataque modifica una copia de la respuesta autentica.
    // Se exige que el verificador rechace el resultado alterado.
    section("C11: omisiones, duplicados y pruebas alteradas");
    {
        auto bad = honest;
        ++bad.count;
        check(!verify(q, bad).accepted, "rechaza COUNT aumentado");
    }
    {
        auto bad = honest;
        // Quitar un identificador y ajustar el contador no prueba completitud.
        bad.references.pop_back();
        bad.count = bad.references.size();
        check(!verify(q, bad).accepted, "rechaza resultado incompleto con COUNT modificado");
    }
    {
        auto bad = honest;
        bad.references.push_back(bad.references.back());
        bad.count = bad.references.size();
        check(!verify(q, bad).accepted, "rechaza referencia duplicada");
    }
    {
        auto bad = honest;
        // Alteracion de split: el complemento ya no coincide con el AIT.
        bad.splits[0][0].rest_g1 = bad.splits[0][0].rest_g1 + crypto::init_pairing_group().g1;
        check(!verify(q, bad).accepted, "rechaza testigo de complemento alterado");
    }
    {
        auto bad = honest;
        // Alteracion de Bezout: no queda certificada la interseccion completa.
        bad.disjoint[0].bezout_u = bad.disjoint[0].bezout_u + crypto::init_pairing_group().g2;
        check(!verify(q, bad).accepted, "rechaza testigo de Bezout alterado");
    }
    {
        auto bad = honest;
        bad.state_id[0] ^= 1;
        check(!verify(q, bad).accepted, "rechaza digest de otro estado");
    }
    check(!verify(make_query(2), honest).accepted, "rechaza consulta de otro alumno");

    // Verifica ausencia autentica de coincidencias y cambio de parametro.
    section("C11: cardinalidad cero y segundo alumno");
    const auto zero = make_query(8);  // sin filas en fixture de prueba
    const auto zero_v = verify(zero, prove(zero));
    check(zero_v.accepted && zero_v.count == 0, "COUNT cero autenticado");
    const auto other = make_query(2);
    const auto other_v = verify(other, prove(other));
    check(other_v.accepted && other_v.count == expected(2), "otro alumno verificado");
    return test::finish();
}

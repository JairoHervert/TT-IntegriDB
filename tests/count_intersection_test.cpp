// count_intersection_test.cpp
//
// C12 verifica COUNT sobre student_id y evaluation_type mediante dos AIT.
// No recupera filas completas de MySQL: la respuesta contiene IDs y ASO.
#include <algorithm>
#include <cstdint>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/query/count_intersection.hpp"
#include "tt/query/response_io.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

/*
 * Comprueba C12 con datos sinteticos autenticados en memoria.
 *
 * Casos:
 *   Conteo independiente, serializacion TTRSP tipo 3, manipulaciones,
 *   consultas diferentes y resultado vacio.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    // Setup construye los mismos AIT del plan 'queries' que el programa real.
    // La prueba no abre ninguna conexion MySQL.
    const auto base = test::case_study_database(160);
    const auto keys = scheme::init(220);
    std::vector<scheme::TreeSpec> trees;
    for (const auto& plan : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas()))
        trees.push_back(plan.spec);
    const auto state = scheme::setup(base, trees, "queries", keys.sk, keys.pk);

    const auto make_query = [](std::int64_t student, std::int64_t evaluation) {
        return query::CountQuery{"C12", {"academic_record", "student_id", student, student},
                                 {"academic_record", "evaluation_type", evaluation, evaluation}};
    };
    // Oraculo independiente: recorre todas las filas en memoria, sin AIT,
    // para obtener el COUNT correcto con el que comparar la prueba ASO.
    const auto expected = [&](const query::CountQuery& q) {
        std::uint64_t total = 0;
        for (const auto& row : base.find("academic_record")->rows) {
            if (std::get<std::int64_t>(row[1]) == q.first.lo &&
                std::get<std::int64_t>(row[4]) == q.second.lo) ++total;
        }
        return total;
    };
    const auto prove = [&](const query::CountQuery& q) {
        return query::answer_count(state.database, state.digest, keys.pk.aso, q).response;
    };
    const auto verify = [&](const query::CountQuery& q, const query::CountResponse& r) {
        return query::verify_count(state.digest, keys.pk.aso, q, r);
    };

    // Caso positivo: verifica cantidad, referencias y certificados ASO.
    // La suma de IDs no sustituye la prueba de completitud.
    section("C12: COUNT correcto, sin filas MySQL ni Merkle por fila");
    const auto q = make_query(1, 1);
    const auto honest = prove(q);
    const auto result = verify(q, honest);
    check(result.accepted && result.count == expected(q) && result.count > 0,
          "el conteo verificado coincide con el recorrido independiente de la base");
    check(result.count == honest.references.size() &&
          !honest.splits[0].empty() && !honest.splits[1].empty() && !honest.disjoint.empty(),
          "prueba contiene los IDs y certificados ASO (sin filas completas)");

    // La respuesta debe sobrevivir al transporte binario y conservar su tipo;
    // lectores incorrectos y buffers truncados o sobrantes deben fallar.
    section("TTRSP tipo 3: serializacion y limites");
    const auto encoded = query::encode_count_response(honest);
    const auto decoded = query::decode_count_response(encoded);
    check(query::encode_count_response(decoded) == encoded && verify(q, decoded).accepted,
          "serializacion canonica y verificacion tras decodificar");
    check_throws<query::ResponseFormatError>([&] { query::decode_response(encoded); },
                                               "el lector C1-C6 rechaza C12");
    check_throws<query::ResponseFormatError>([&] { query::decode_multirange_response(encoded); },
                                               "el lector C7 rechaza C12");
    auto truncated = encoded;
    truncated.pop_back();
    check_throws<query::ResponseFormatError>([&] { query::decode_count_response(truncated); },
                                               "C12 truncada se rechaza");
    auto extra = encoded;
    extra.push_back(0);
    check_throws<query::ResponseFormatError>([&] { query::decode_count_response(extra); },
                                               "C12 con bytes extra se rechaza");

    // Se manipula una copia de una prueba autentica por caso.
    // Deben rechazarse tanto omisiones encubiertas como testigos falsos.
    section("Servidor malicioso: cambios en COUNT, IDs y pruebas");
    {
        auto bad = honest;
        ++bad.count;
        check(!verify(q, bad).accepted, "COUNT declarado aumentado");
    }
    {
        auto bad = honest;
        // Reducir COUNT junto con la lista no justifica omitir una coincidencia.
        bad.references.pop_back();
        bad.count = bad.references.size();
        check(!verify(q, bad).accepted, "se omite un ID aunque se ajuste COUNT");
    }
    {
        auto bad = honest;
        bad.references.push_back(bad.references.back());
        bad.count = bad.references.size();
        check(!verify(q, bad).accepted, "ID duplicado rechazado");
    }
    {
        auto bad = honest;
        // Desplazar la asignacion de un ID a otro nodo sin cambiar el COUNT.
        if (!bad.node_references[0].empty()) {
            bad.node_references[0][0].push_back(-99);
            check(!verify(q, bad).accepted, "ID adicional inventado en una cobertura");
        }
    }
    {
        auto bad = honest;
        // Un compromiso de complemento alterado rompe la prueba de split.
        bad.splits[0][0].rest_g1 = bad.splits[0][0].rest_g1 + crypto::init_pairing_group().g1;
        check(!verify(q, bad).accepted, "acumulador de complemento alterado");
    }
    {
        auto bad = honest;
        // Un certificado de Bezout falso rompe la prueba de completitud.
        bad.disjoint[0].bezout_v = bad.disjoint[0].bezout_v + crypto::init_pairing_group().g2;
        check(!verify(q, bad).accepted, "testigo Bezout adulterado");
    }
    {
        auto bad = honest;
        bad.state_id[0] ^= 1;
        check(!verify(q, bad).accepted, "digest cambiado");
    }
    check(!verify(make_query(2, 1), honest).accepted, "consulta diferente a la solicitada");

    // COUNT=0 sigue requiriendo una prueba autentica del estado publicado.
    section("Resultados vacios y otros parametros");
    const auto zero = make_query(8, 1); // alumno 8 no tiene registros en fixture
    const auto empty = verify(zero, prove(zero));
    check(empty.accepted && empty.count == 0, "COUNT cero autenticado");
    const auto other = make_query(3, 2);
    const auto other_v = verify(other, prove(other));
    check(other_v.accepted && other_v.count == expected(other), "segunda combinacion alumno/evaluacion");
    return test::finish();
}

// Pruebas de C7: interseccion verificable de dos AIT mediante ASO.
// Incluye ataques que omiten una fila (la prueba de Bezout debe fallar).
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/ait/node.hpp"
#include "tt/query/multirange.hpp"
#include "tt/query/response_io.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

int main() {
    const auto base = test::case_study_database(120);
    const auto keys = scheme::init(160);
    std::vector<scheme::TreeSpec> specs;
    for (const auto& plan : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas())) {
        specs.push_back(plan.spec);
    }
    const auto state = scheme::setup(base, specs, "queries", keys.sk, keys.pk);
    db::MemoryRowStore store(base);
    const auto query_c7 = [](std::int64_t gl, std::int64_t gh, std::int64_t tl, std::int64_t th) {
        return query::MultiRangeQuery{{"academic_record", "grade", gl, gh},
                                      {"academic_record", "term_code", tl, th}};
    };
    const auto expected = [&](const query::MultiRangeQuery& q) {
        std::vector<db::Row> rows;
        const auto* t = base.find("academic_record");
        for (const auto& row : t->rows) {
            const auto grade = std::get<std::int64_t>(row[5]);
            const auto term = std::get<std::int64_t>(row[3]);
            if (q.first.lo <= grade && grade <= q.first.hi &&
                q.second.lo <= term && term <= q.second.hi) rows.push_back(row);
        }
        // C7 devuelve el resultado en orden del AIT de grade: (grade, record_id).
        std::sort(rows.begin(), rows.end(), [](const db::Row& a, const db::Row& b) {
            return std::make_pair(std::get<std::int64_t>(a[5]), std::get<std::int64_t>(a[0])) <
                   std::make_pair(std::get<std::int64_t>(b[5]), std::get<std::int64_t>(b[0]));
        });
        return rows;
    };
    const auto proof = [&](const query::MultiRangeQuery& q) {
        return query::answer_multirange(state.database, state.digest, keys.pk.aso, q, store).response;
    };
    const auto verify = [&](const query::MultiRangeQuery& q, const query::MultiRangeResponse& r) {
        return query::verify_multirange(state.digest, keys.pk.aso, q, r);
    };

    section("C7 autentica exactamente la interseccion de dos conjuntos de identificadores");
    const auto q = query_c7(6, 8, 20241, 20251);
    const auto honest = proof(q);
    const auto verified = verify(q, honest);
    check(verified.accepted && verified.rows == expected(q) && !verified.rows.empty(),
          "dos RangeCover + ASO + Merkle: igual al recorrido de la base");
    check(!honest.splits[0].empty() && !honest.splits[1].empty() && !honest.disjoint.empty(),
          "la prueba realmente incluye certificados criptograficos de split y coprimalidad");

    section("ASO rechaza un conjunto residual que oculta una coincidencia");
    {
        // Si se omite ID=3, ese ID acaba en AMBOS complementos.
        // No existen polinomios de Bezout U,V que satisfagan U*P_A+V*P_B=1.
        aso::FieldSet a, b;
        for (int id : {1, 2, 3}) a.elements.push_back(ait::value_to_element(id));
        for (int id : {3, 4}) b.elements.push_back(ait::value_to_element(id));
        check_throws<std::invalid_argument>([&] { aso::prove_disjoint(a, b, keys.pk.aso); },
                                            "un ID oculto en ambos complementos impide construir el certificado");
        a.elements.pop_back();  // {1, 2}
        b.elements.erase(b.elements.begin());  // {4}
        const auto wa = aso::prove_split(a, keys.pk.aso);
        const auto wb = aso::prove_split(b, keys.pk.aso);
        const auto witness = aso::prove_disjoint(a, b, keys.pk.aso);
        check(aso::verify_disjoint(wa, wb, witness), "residuales disjuntos tienen certificado valido");
    }

    section("Formato serializado de tipo 2, separado de C1-C6");
    const auto wire = query::encode_multirange_response(honest);
    const auto decoded = query::decode_multirange_response(wire);
    check(query::encode_multirange_response(decoded) == wire && verify(q, decoded).accepted,
          "serializacion canonica C7 y verificacion tras recuperar bytes");
    check_throws<query::ResponseFormatError>([&] { query::decode_response(wire); },
                                               "el lector C1-C6 rechaza una respuesta C7");
    auto malformed = wire;
    malformed.pop_back();
    check_throws<query::ResponseFormatError>([&] { query::decode_multirange_response(malformed); },
                                               "respuesta C7 truncada");
    malformed = wire;
    malformed.push_back(0);
    check_throws<query::ResponseFormatError>([&] { query::decode_multirange_response(malformed); },
                                               "respuesta C7 con bytes extra");

    section("Probador malicioso: omisiones, alteraciones, pruebas incorrectas");
    {
        auto bad = honest;
        bad.rows.pop_back();
        check(!verify(q, bad).accepted, "omitir una fila invalida ASO: completitud");
    }
    {
        auto bad = honest;
        bad.rows.push_back(bad.rows.front());
        check(!verify(q, bad).accepted, "fila duplicada rechazada");
    }
    {
        auto bad = honest;
        std::get<std::int64_t>(bad.rows[0].row[5]) = 99;
        check(!verify(q, bad).accepted, "fila manipulada en su calificacion rechazada");
    }
    {
        auto bad = honest;
        bad.splits[0][0].rest_g1 = bad.splits[0][0].rest_g1 + crypto::init_pairing_group().g1;
        check(!verify(q, bad).accepted, "un compromiso de complemento falso se rechaza");
    }
    {
        auto bad = honest;
        bad.disjoint[0].bezout_u = bad.disjoint[0].bezout_u + crypto::init_pairing_group().g2;
        check(!verify(q, bad).accepted, "testigo de Bezout alterado se rechaza");
    }
    {
        auto bad = honest;
        bad.state_id[0] ^= 1;
        check(!verify(q, bad).accepted, "respuesta con digest diferente se rechaza");
    }
    {
        auto different = query_c7(5, 8, 20241, 20251);
        check(!verify(different, honest).accepted, "consulta distinta de la solicitada se rechaza");
    }
    {
        auto bad = honest;
        bad.ranges[0].entries[0].hash[0] ^= 1;
        // Segun el tipo del nodo puede no usar hash, por lo que se exige
        // ademas una manipulacion efectiva del acumulador autenticado.
        for (auto& e : bad.ranges[0].entries) {
            if (e.kind == ait::EntryKind::kCover) {
                e.payload.acc_g1 = e.payload.acc_g1 + crypto::init_pairing_group().g1;
                break;
            }
        }
        check(!verify(q, bad).accepted, "AIT adulterado rechazado");
    }

    section("Conjuntos vacios y casos frontera");
    {
        const auto none = query_c7(11, 15, 20241, 20251);
        const auto v = verify(none, proof(none));
        check(v.accepted && v.rows.empty(), "primer rango vacio, interseccion vacia autenticada");
    }
    {
        const auto none = query_c7(6, 8, 20301, 20310);
        const auto v = verify(none, proof(none));
        check(v.accepted && v.rows.empty(), "segundo rango vacio, interseccion vacia autenticada");
    }
    {
        const auto all = query_c7(1, 20, 20201, 20301);
        const auto v = verify(all, proof(all));
        check(v.accepted && v.rows == expected(all) && v.rows.size() == 120,
              "interseccion que incluye toda la tabla");
    }
    return test::finish();
}

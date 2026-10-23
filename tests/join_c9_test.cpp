// Pruebas de C9: JOIN verificable con claves repetidas en student.
// Cubre completitud de ambas tablas, Merkle, ASO, Bezout y serializacion.

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
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
 * Comprueba C9 sobre bases sinteticas independientes de MySQL.
 *
 * Casos:
 *   Multiplicidad SQL de alumnos, programas sin alumnos, alumnos huerfanos,
 *   resultado vacio, bytes invalidos y omisiones/manipulaciones de pruebas.
 *
 * Devuelve:
 *   0 si todas las comprobaciones se satisfacen, 1 en otro caso.
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
    const auto honest = query::answer_join_program(state.database, state.digest, keys.pk.aso, store).response;
    const auto verify = [&](const query::JoinProgramResponse& r) {
        return query::verify_join_program(state.digest, keys.pk.aso, r);
    };

    section("C9: JOIN completo y repeticion de program_id entre alumnos");
    const auto valid = verify(honest);
    check(valid.accepted && valid.joined_rows == 8 && valid.program_rows.size() == 3,
          "8 alumnos y 3 programas, cada alumno aparece exactamente una vez");
    check(honest.student_rows.size() == 8 && honest.program_rows.size() == 3,
          "respuesta incluye filas Merkle de ambas tablas y no solo IDs");
    // Oraculo independiente: busca cada alumno en la base original y
    // comprueba que su fila integra aparece exactamente una vez en la salida.
    bool matches_sql = valid.accepted && valid.student_rows.size() == base.tables[3].rows.size();
    for (const auto& expected : base.tables[3].rows) {
        matches_sql = matches_sql && std::count(valid.student_rows.begin(), valid.student_rows.end(), expected) == 1;
    }
    check(matches_sql, "filas completas coinciden exactamente con el JOIN evaluado sobre la base original");

    section("C9: serializacion TTRSP tipo 5");
    const auto bytes = query::encode_join_program_response(honest);
    const auto decoded = query::decode_join_program_response(bytes);
    check(verify(decoded).accepted, "ida y vuelta del formato binario conserva las pruebas");
    {
        auto broken = bytes;
        broken.push_back(0);
        check_throws<query::ResponseFormatError>([&]{ (void)query::decode_join_program_response(broken); },
                                                 "se rechazan bytes adicionales");
    }
    {
        auto broken = bytes;
        broken.pop_back();
        check_throws<query::ResponseFormatError>([&]{ (void)query::decode_join_program_response(broken); },
                                                 "se rechaza una respuesta truncada");
    }

    section("C9: ataques de omision y manipulacion criptografica");
    {
        auto bad = honest;
        bad.student_rows.pop_back();
        check(!verify(bad).accepted, "omitir un alumno coincidente debe rechazarse");
    }
    {
        auto bad = honest;
        bad.program_rows.pop_back();
        check(!verify(bad).accepted, "omitir un programa con alumnos debe rechazarse");
    }
    {
        auto bad = honest;
        bad.student_rows.push_back(bad.student_rows.front());
        check(!verify(bad).accepted, "duplicar un alumno debe rechazarse");
    }
    {
        auto bad = honest;
        std::get<std::string>(bad.student_rows.front().row[2]) = "ALTERADO";
        check(!verify(bad).accepted, "alterar el nombre rompe la prueba Merkle");
    }
    {
        auto bad = honest;
        bad.splits[0].rest_g1.clear();
        check(!verify(bad).accepted, "testigo ASO falso debe rechazarse");
    }
    {
        auto bad = honest;
        bad.disjoint[0].bezout_u.clear();
        check(!verify(bad).accepted, "testigo Bezout falso debe rechazarse");
    }
    {
        auto bad = honest;
        bad.state_id[0] ^= 1;
        check(!verify(bad).accepted, "digest de un estado distinto debe rechazarse");
    }
    {
        auto bad = honest;
        bad.ranges[0].entries.clear();
        check(!verify(bad).accepted, "AIT ausente debe rechazarse");
    }

    section("C9: JOIN no exige que todas las claves coincidan");
    {
        auto partial = base;
        // El alumno 8 pasa a un programa ausente del catalogo de programas.
        std::get<std::int64_t>(partial.tables[3].rows[7][5]) = 999;
        const auto st = scheme::setup(partial, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(partial);
        const auto ans = query::answer_join_program(st.database, st.digest, keys.pk.aso, source).response;
        const auto v = query::verify_join_program(st.digest, keys.pk.aso, ans);
        check(v.accepted && v.joined_rows == 7 && ans.student_rows.size() == 7,
              "alumno sin programa correspondiente no forma parte del INNER JOIN");
    }
    {
        auto partial = base;
        // Agrega un programa sin estudiantes; el resultado no debe incluirlo.
        partial.tables[2].rows.push_back({test::int_cell(4), test::text_cell("OTR"),
                                                   test::text_cell("Programa sin alumnos")});
        const auto st = scheme::setup(partial, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(partial);
        const auto ans = query::answer_join_program(st.database, st.digest, keys.pk.aso, source).response;
        const auto v = query::verify_join_program(st.digest, keys.pk.aso, ans);
        check(v.accepted && v.program_rows.size() == 3 && v.joined_rows == 8,
              "programa sin alumnos es excluido, pero la prueba sigue siendo completa");
    }
    {
        auto empty = base;
        empty.tables[3].rows.clear();
        const auto st = scheme::setup(empty, specs, "queries", keys.sk, keys.pk);
        db::MemoryRowStore source(empty);
        const auto ans = query::answer_join_program(st.database, st.digest, keys.pk.aso, source).response;
        const auto v = query::verify_join_program(st.digest, keys.pk.aso, ans);
        check(v.accepted && v.joined_rows == 0 && v.program_rows.empty(),
              "tabla student vacia da JOIN vacio autenticado");
    }
    return test::finish();
}

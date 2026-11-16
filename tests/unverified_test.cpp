// unverified_test.cpp
//
// Pruebas de las consultas no verificables N1 a N5: lectura estricta, SQL
// de las plantillas, separacion con el catalogo verificable y formato TTUNV
// (que el verificador rechace un resultado con otra forma o alterado).

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

#include "test_support.hpp"
#include "tt/io/binary.hpp"
#include "tt/query/query.hpp"
#include "tt/query/unverified.hpp"

namespace {

using namespace tt;
using query::QueryError;
using query::UnverifiedQuery;
using query::UnverifiedResult;
using test::check;
using test::check_throws;
using test::section;

/*
 * Lee una consulta N escrita como en la linea de comandos.
 */
UnverifiedQuery parse(const std::vector<std::string>& words) { return query::parse_unverified_query(words); }

/*
 * Resultado valido de N2 con dos periodos, para las pruebas del formato.
 */
UnverifiedResult sample_n2(const UnverifiedQuery& q) {
    UnverifiedResult r;
    r.catalog_id = q.catalog_id;
    r.columns = query::unverified_columns(q);
    r.rows.push_back({std::int64_t{20241}, std::int64_t{6}, 8.5});
    r.rows.push_back({std::int64_t{20242}, std::int64_t{2}, std::monostate{}});
    r.row_count = 2;
    return r;
}

}  // namespace

/*
 * Ejecuta las comprobaciones y devuelve 0 si todas pasan.
 */
int main() {
    section("Lectura de N1 a N5");
    {
        const UnverifiedQuery n1 = parse({"N1", "Garc"});
        check(n1.kind == query::UnverifiedKind::kSurnamePrefix && n1.text_value == "Garc", "N1 con prefijo");
        check(parse({"N2", "7"}).number == 7 && parse({"N3", "1"}).catalog_id == "N3" &&
                  parse({"N4", "500"}).kind == query::UnverifiedKind::kFullTranscript && parse({"N5", "100"}).number == 100,
              "N2 a N5 con su parametro");
        check(query::is_unverified_query({"N3", "1"}) && !query::is_unverified_query({"C13", "1"}) &&
                  !query::is_unverified_query({}),
              "is_unverified_query distingue N de C");
    }

    section("Valores que no cambian la forma del SQL");
    check_throws<QueryError>([] { parse({"N1", "G%"}); }, "N1 rechaza comodines de LIKE");
    check_throws<QueryError>([] { parse({"N1", "O'Brien"}); }, "N1 rechaza comillas");
    check_throws<QueryError>([] { parse({"N1", "Garc;DROP"}); }, "N1 rechaza punto y coma");
    check_throws<QueryError>([] { parse({"N1", std::string(31, 'a')}); }, "N1 rechaza mas de 30 letras");
    check_throws<QueryError>([] { parse({"N2", "-1"}); }, "N2 rechaza negativos");
    check_throws<QueryError>([] { parse({"N3", "01"}); }, "N3 exige forma canonica");
    check_throws<QueryError>([] { parse({"N4", "1", "2"}); }, "N4 con un valor de sobra");
    check_throws<QueryError>([] { parse({"N5", "0"}); }, "N5 exige al menos 1");
    check_throws<QueryError>([] { parse({"N5", "101"}); }, "N5 limita a 100 alumnos");
    check_throws<QueryError>([] { parse({"N9", "1"}); }, "id inexistente");

    section("Plantillas SQL");
    check(query::unverified_sql(parse({"N1", "Garc"})).find("LIKE 'Garc%'") != std::string::npos,
          "N1 usa el prefijo validado en LIKE");
    check(query::unverified_sql(parse({"N3", "4"})) == "SELECT MAX(grade) FROM academic_record WHERE student_id = 4",
          "N3 es MAX por alumno");
    check(query::unverified_sql(parse({"N5", "10"})).find("LIMIT 10") != std::string::npos,
          "N5 termina con LIMIT");
    for (const char* id : {"N1", "N2", "N3", "N4", "N5"}) {
        const UnverifiedQuery q = parse({id, id[1] == '1' ? "Lopez" : "3"});
        check(query::unverified_sql(q).rfind("SELECT ", 0) == 0 && !query::unverified_columns(q).empty() &&
                  !query::unverified_reason(q).empty(),
              std::string(id) + ": SELECT, columnas y motivo");
    }

    section("El catalogo verificable no acepta N1 a N5");
    check_throws<QueryError>([] { query::parse_query({"N3", "1"}); }, "parse_query rechaza N3");
    check_throws<QueryError>([] { parse({"C13", "1"}); }, "parse_unverified_query rechaza C13");

    section("Formato TTUNV");
    {
        const UnverifiedQuery q = parse({"N2", "1"});
        const UnverifiedResult r = sample_n2(q);
        const std::vector<unsigned char> bytes = query::encode_unverified_result(r);
        const UnverifiedResult back = query::decode_unverified_result(bytes, q);
        check(back.catalog_id == "N2" && back.columns == r.columns && back.rows == r.rows && back.row_count == 2,
              "ida y vuelta conserva id, columnas, filas y NULL");

        check_throws<io::FormatError>([&] { query::decode_unverified_result(bytes, parse({"N3", "1"})); },
                                      "resultado de otra consulta");
        std::vector<unsigned char> extra = bytes;
        extra.push_back(0);
        check_throws<io::FormatError>([&] { query::decode_unverified_result(extra, q); }, "bytes sobrantes");
        std::vector<unsigned char> cut(bytes.begin(), bytes.end() - 3);
        check_throws<io::FormatError>([&] { query::decode_unverified_result(cut, q); }, "resultado truncado");
        std::vector<unsigned char> magic = bytes;
        magic[0] = 'X';
        check_throws<io::FormatError>([&] { query::decode_unverified_result(magic, q); }, "marca distinta");

        UnverifiedResult renamed = r;
        renamed.columns[2].name = "verified";
        check_throws<io::FormatError>(
            [&] { query::decode_unverified_result(query::encode_unverified_result(renamed), q); },
            "columnas cambiadas por el servidor");
        UnverifiedResult wrong_type = r;
        wrong_type.rows[0][0] = std::string("20241");
        check_throws<io::FormatError>(
            [&] { query::decode_unverified_result(query::encode_unverified_result(wrong_type), q); },
            "celda de otro tipo");
        UnverifiedResult nan = r;
        nan.rows[0][2] = std::numeric_limits<double>::quiet_NaN();
        check_throws<io::FormatError>(
            [&] { query::decode_unverified_result(query::encode_unverified_result(nan), q); }, "real no finito");
        UnverifiedResult more = r;
        more.row_count = 1;
        check_throws<io::FormatError>(
            [&] { query::decode_unverified_result(query::encode_unverified_result(more), q); },
            "mas filas que las declaradas");

        UnverifiedResult partial = r;
        partial.row_count = 50;
        check(query::decode_unverified_result(query::encode_unverified_result(partial), q).row_count == 50,
              "un resultado recortado conserva el total declarado");
    }

    return test::finish();
}

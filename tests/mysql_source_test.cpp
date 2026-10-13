// mysql_source_test.cpp
//
// Pruebas del adaptador MySQL. Las conversiones (tipos y enteros) se
// prueban siempre. La lectura real de la base solo se prueba si se define
// TT_TEST_MYSQL=1 junto con las variables de conexion TT_DB_*, para que
// ctest no dependa de tener MySQL encendido:
//
//   TT_TEST_MYSQL=1 TT_DB_PASS=app_pass_2026 ctest --test-dir build -R mysql

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/db/row_hash.hpp"
#include "tt/mysql/database_source.hpp"
#include "tt/mysql/row_store.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace {

/*
 * Indica si parse_mysql_integer acepta el texto y da el valor esperado.
 */
bool parses_to(const std::string& text, std::int64_t expected) {
    std::int64_t v = 0;
    return tt::mysql::parse_mysql_integer(text, v) && v == expected;
}

/*
 * Indica si parse_mysql_integer rechaza el texto.
 */
bool rejects(const std::string& text) {
    std::int64_t v = 0;
    return !tt::mysql::parse_mysql_integer(text, v);
}

/*
 * Hashes de todas las filas de una base, tabla por tabla (para comparar
 * dos lecturas).
 */
std::vector<tt::crypto::Digest> all_row_hashes(const tt::db::Database& d) {
    std::vector<tt::crypto::Digest> hashes;
    for (const tt::db::Table& t : d.tables) {
        for (const tt::db::Row& row : t.rows) hashes.push_back(tt::db::row_hash(t.schema, row));
    }
    return hashes;
}

}  // namespace

/*
 * Pruebas del adaptador: traduccion de tipos de MySQL, conversion estricta
 * de enteros y, si esta habilitado, lectura de la base del caso de estudio
 * (estructura igual a 01_schema.sql, datos validos y dos lecturas
 * identicas).
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    using tt::db::ColumnType;
    using tt::test::check;
    using tt::test::section;

    section("Tipos de MySQL");
    {
        check(tt::mysql::column_type_from_mysql("int") == ColumnType::kInteger &&
                  tt::mysql::column_type_from_mysql("tinyint") == ColumnType::kInteger &&
                  tt::mysql::column_type_from_mysql("BIGINT") == ColumnType::kInteger,
              "los enteros se traducen a integer");
        check(tt::mysql::column_type_from_mysql("varchar") == ColumnType::kText &&
                  tt::mysql::column_type_from_mysql("text") == ColumnType::kText,
              "CHAR, VARCHAR y TEXT se traducen a text");
        check(!tt::mysql::column_type_from_mysql("decimal") && !tt::mysql::column_type_from_mysql("date") &&
                  !tt::mysql::column_type_from_mysql("float") && !tt::mysql::column_type_from_mysql("blob"),
              "DECIMAL, DATE, FLOAT y BLOB no estan soportados");
    }

    section("Enteros como texto");
    {
        check(parses_to("0", 0) && parses_to("75", 75) && parses_to("-12", -12) && parses_to("2024630001", 2024630001),
              "enteros normales");
        check(parses_to("9223372036854775807", std::numeric_limits<std::int64_t>::max()) &&
                  parses_to("-9223372036854775808", std::numeric_limits<std::int64_t>::min()),
              "los extremos de int64");
        check(rejects("9223372036854775808") && rejects("18446744073709551615") && rejects("-9223372036854775809"),
              "fuera de int64 (por ejemplo BIGINT UNSIGNED grande) se rechaza");
        check(rejects("") && rejects("-") && rejects("007") && rejects("-0") && rejects("1.5") && rejects(" 1") &&
                  rejects("+1") && rejects("1e3"),
              "formatos no canonicos se rechazan");
    }

    section("Lectura de la base (TT_TEST_MYSQL=1)");
    const char* enabled = std::getenv("TT_TEST_MYSQL");
    if (enabled == nullptr || std::string(enabled) != "1") {
        std::printf("  (omitida: define TT_TEST_MYSQL=1 y las variables TT_DB_* para probar contra MySQL)\n");
        return tt::test::finish();
    }
    tt::mysql::DatabaseSource source(tt::mysql::ConnectionConfig::from_environment());
    const tt::db::Database first = source.read();
    std::vector<tt::db::TableSchema> schemas;
    for (const tt::db::Table& t : first.tables) schemas.push_back(t.schema);
    check(schemas == tt::test::case_study_schemas(),
          "la estructura leida es la de 01_schema.sql (tablas, columnas, tipos y llave primaria)");
    tt::db::validate(first);
    std::size_t rows = 0;
    for (const tt::db::Table& t : first.tables) rows += t.rows.size();
    check(rows > 0, "hay datos (" + std::to_string(rows) + " filas)");
    check(all_row_hashes(source.read()) == all_row_hashes(first), "dos lecturas dan exactamente las mismas filas");
    tt::scheme::plan_trees(tt::scheme::Preset::kQueries, schemas);
    check(true, "el plan queries se puede aplicar a esta base");

    section("Filas por referencia para el probador (TT_TEST_MYSQL=1)");
    {
        // Algunas filas de cada tabla, pedidas en orden inverso: deben llegar
        // iguales a las de la lectura completa y en el orden pedido.
        tt::mysql::DatabaseRowStore store(tt::mysql::ConnectionConfig::from_environment());
        bool same = true;
        for (const tt::db::Table& t : first.tables) {
            std::vector<std::int64_t> refs;
            std::vector<tt::db::Row> expected;
            for (std::size_t i = t.rows.size(); i-- > 0 && refs.size() < 2500;) {
                refs.push_back(std::get<std::int64_t>(t.rows[i][t.schema.reference_column]));
                expected.push_back(t.rows[i]);
            }
            if (store.fetch_rows(t.schema, refs) != expected) same = false;
        }
        check(same, "las filas pedidas por referencia (en bloques de 1000) son las de la base, en el orden pedido");
        check(store.fetch_rows(first.tables[0].schema, {}).empty(), "sin referencias no se consulta nada");
        bool missing = false;
        try {
            store.fetch_rows(first.tables[0].schema, {std::numeric_limits<std::int64_t>::max()});
        } catch (const tt::db::DataError&) {
            missing = true;
        }
        check(missing, "una referencia que no existe se informa como error");
    }
    return tt::test::finish();
}

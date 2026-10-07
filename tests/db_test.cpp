// db_test.cpp
//
// Pruebas del modelo relacional del esquema: validacion de tablas y filas,
// y codificacion canonica del hash de fila.

#include <cstdint>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "tt/db/row_hash.hpp"
#include "tt/db/source.hpp"
#include "tt/db/table.hpp"

namespace {

using tt::db::Cell;
using tt::db::Column;
using tt::db::ColumnType;
using tt::db::Database;
using tt::db::DataError;
using tt::db::Row;
using tt::db::Table;
using tt::db::TableSchema;

/*
 * Tabla de ejemplo parecida a course: course_id (referencia), course_code
 * (texto) y credits_tenths.
 */
Table sample_course() {
    Table t;
    t.schema.name = "course";
    t.schema.columns = {{"course_id", ColumnType::kInteger},
                        {"course_code", ColumnType::kText},
                        {"credits_tenths", ColumnType::kInteger}};
    t.schema.reference_column = 0;
    t.rows = {{Cell{std::int64_t{1}}, Cell{std::string("ISC101")}, Cell{std::int64_t{75}}},
              {Cell{std::int64_t{2}}, Cell{std::string("ISC102")}, Cell{std::int64_t{105}}}};
    return t;
}

/*
 * Base con una sola tabla.
 */
Database single(const Table& t) { return Database{{t}}; }

}  // namespace

/*
 * Pruebas del modelo: identificadores, validacion de estructura y de filas,
 * y propiedades del hash de fila (determinista, sensible a cada celda, sin
 * confundir tipos, orden ni fronteras entre textos).
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    using tt::test::check;
    using tt::test::check_throws;
    using tt::test::section;

    section("Identificadores y tipos");
    {
        check(tt::db::is_valid_identifier("academic_record") && tt::db::is_valid_identifier("_x9"),
              "nombres con letras, digitos y guion bajo son validos");
        check(!tt::db::is_valid_identifier("") && !tt::db::is_valid_identifier("9abc") &&
                  !tt::db::is_valid_identifier("a b") && !tt::db::is_valid_identifier("a`b") &&
                  !tt::db::is_valid_identifier(std::string(65, 'a')),
              "vacio, inicio con digito, espacios, comillas y mas de 64 caracteres se rechazan");
        check(tt::db::parse_column_type("integer") == ColumnType::kInteger &&
                  tt::db::parse_column_type("text") == ColumnType::kText && !tt::db::parse_column_type("int"),
              "los tipos se leen solo como 'integer' o 'text'");
    }

    section("Validacion");
    {
        tt::db::validate(single(sample_course()));
        check(true, "una tabla correcta pasa la validacion");

        Table no_ref = sample_course();
        no_ref.schema.reference_column = 1;
        check_throws<DataError>([&] { tt::db::validate(single(no_ref)); }, "la referencia no puede ser de texto");

        Table dup_col = sample_course();
        dup_col.schema.columns[2].name = "course_id";
        check_throws<DataError>([&] { tt::db::validate(single(dup_col)); }, "columnas repetidas se rechazan");

        Table dup_ref = sample_course();
        dup_ref.rows[1][0] = Cell{std::int64_t{1}};
        check_throws<DataError>([&] { tt::db::validate(single(dup_ref)); },
                                "valores repetidos en la columna de referencia se rechazan");

        Table short_row = sample_course();
        short_row.rows[0].pop_back();
        check_throws<DataError>([&] { tt::db::validate(single(short_row)); },
                                "una fila con celdas de menos se rechaza");

        Table wrong_type = sample_course();
        wrong_type.rows[0][2] = Cell{std::string("75")};
        check_throws<DataError>([&] { tt::db::validate(single(wrong_type)); },
                                "texto en una columna entera se rechaza");

        Database twice{{sample_course(), sample_course()}};
        check_throws<DataError>([&] { tt::db::validate(twice); }, "dos tablas con el mismo nombre se rechazan");

        tt::db::MemorySource source(single(sample_course()));
        check(source.read().tables.size() == 1 && source.read().find("course") != nullptr,
              "MemorySource devuelve la base que se le dio");
    }

    section("Hash de fila");
    {
        const Table t = sample_course();
        const Row& row = t.rows[0];
        check(tt::db::row_hash(t.schema, row) == tt::db::row_hash(t.schema, row), "es deterministico");

        bool each_cell = true;
        Row changed = row;
        changed[0] = Cell{std::int64_t{9}};
        each_cell = each_cell && !(tt::db::row_hash(t.schema, changed) == tt::db::row_hash(t.schema, row));
        changed = row;
        changed[1] = Cell{std::string("ISC10l")};
        each_cell = each_cell && !(tt::db::row_hash(t.schema, changed) == tt::db::row_hash(t.schema, row));
        changed = row;
        changed[2] = Cell{std::int64_t{76}};
        each_cell = each_cell && !(tt::db::row_hash(t.schema, changed) == tt::db::row_hash(t.schema, row));
        check(each_cell, "cambiar cualquier celda cambia el hash");

        TableSchema other = t.schema;
        other.name = "program";
        check(!(tt::db::row_hash(other, row) == tt::db::row_hash(t.schema, row)),
              "la misma fila en otra tabla da otro hash");

        TableSchema two_texts{"t", {{"id", ColumnType::kInteger}, {"a", ColumnType::kText}, {"b", ColumnType::kText}}, 0};
        const Row ab_c{Cell{std::int64_t{1}}, Cell{std::string("ab")}, Cell{std::string("c")}};
        const Row a_bc{Cell{std::int64_t{1}}, Cell{std::string("a")}, Cell{std::string("bc")}};
        check(!(tt::db::row_hash(two_texts, ab_c) == tt::db::row_hash(two_texts, a_bc)),
              "(\"ab\", \"c\") y (\"a\", \"bc\") dan hashes distintos");
        // Con el byte 0x01 (la marca de texto) dentro de las cadenas, solo la
        // longitud distingue donde termina cada celda.
        const Row left{Cell{std::int64_t{1}}, Cell{std::string("a\x01" "b")}, Cell{std::string("c")}};
        const Row right{Cell{std::int64_t{1}}, Cell{std::string("a")}, Cell{std::string("b\x01" "c")}};
        check(!(tt::db::row_hash(two_texts, left) == tt::db::row_hash(two_texts, right)),
              "textos que contienen la marca de tipo tampoco se confunden (la longitud separa las celdas)");

        TableSchema as_int{"t", {{"id", ColumnType::kInteger}, {"x", ColumnType::kInteger}}, 0};
        TableSchema as_text{"t", {{"id", ColumnType::kInteger}, {"x", ColumnType::kText}}, 0};
        check(!(tt::db::row_hash(as_int, Row{Cell{std::int64_t{1}}, Cell{std::int64_t{7}}}) ==
                tt::db::row_hash(as_text, Row{Cell{std::int64_t{1}}, Cell{std::string("7")}})),
              "el entero 7 y el texto \"7\" dan hashes distintos");

        check_throws<DataError>([&] { tt::db::row_hash(t.schema, Row{Cell{std::int64_t{1}}}); },
                                "una fila con celdas de menos no se puede hashear");
        check_throws<DataError>(
            [&] {
                tt::db::row_hash(t.schema, Row{Cell{std::int64_t{1}}, Cell{std::int64_t{2}}, Cell{std::int64_t{3}}});
            },
            "una celda con el tipo equivocado no se puede hashear");
        check(tt::crypto::to_hex(tt::db::row_hash(t.schema, row)).size() == 64, "el hash tiene 32 bytes");
    }

    return tt::test::finish();
}

// table.cpp

#include "tt/db/table.hpp"

#include <set>
#include <string>
#include <unordered_set>

namespace tt::db {

namespace {

/*
 * Describe una fila para los mensajes de error: tabla, posicion en la lista
 * y, si ya se puede leer, el valor de su columna de referencia.
 */
std::string describe_row(const TableSchema& schema, std::size_t position, const Row& row) {
    std::string text = "tabla " + schema.name + ", fila " + std::to_string(position + 1);
    if (schema.reference_column < row.size()) {
        if (const auto* ref = std::get_if<std::int64_t>(&row[schema.reference_column])) {
            text += " (" + schema.columns[schema.reference_column].name + " = " + std::to_string(*ref) + ")";
        }
    }
    return text;
}

}  // namespace

/*
 * Implementacion: busqueda lineal (las tablas tienen pocas columnas).
 */
std::optional<std::size_t> TableSchema::column_index(const std::string& column) const {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name == column) return i;
    }
    return std::nullopt;
}

/*
 * Implementacion: busqueda lineal por nombre.
 */
const Table* Database::find(const std::string& table) const {
    for (const Table& t : tables) {
        if (t.schema.name == table) return &t;
    }
    return nullptr;
}

/*
 * Implementacion: revisa longitud, primer caracter y el resto uno por uno.
 */
bool is_valid_identifier(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;

    /*
     * Indica si el caracter es una letra ASCII o guion bajo.
     */
    auto is_letter =[](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!is_letter(name[0])) return false;
    for (char c : name) {
        if (!is_letter(c) && !(c >= '0' && c <= '9')) return false;
    }
    return true;
}

/*
 * Implementacion: un nombre fijo por cada valor del enum.
 */
const char* column_type_name(ColumnType type) {
    return type == ColumnType::kInteger ? "integer" : "text";
}

/*
 * Implementacion: compara con los dos nombres posibles.
 */
std::optional<ColumnType> parse_column_type(const std::string& name) {
    if (name == "integer") return ColumnType::kInteger;
    if (name == "text") return ColumnType::kText;
    return std::nullopt;
}

/*
 * Implementacion: revisa el nombre de la tabla, luego cada columna (con un
 * conjunto para detectar repetidos) y al final la columna de referencia.
 */
void validate_schema(const TableSchema& schema) {
    if (!is_valid_identifier(schema.name)) {
        throw DataError("nombre de tabla invalido: '" + schema.name + "'");
    }
    if (schema.columns.empty()) throw DataError("la tabla " + schema.name + " no tiene columnas");
    std::set<std::string> seen;
    for (const Column& c : schema.columns) {
        if (!is_valid_identifier(c.name)) {
            throw DataError("tabla " + schema.name + ": nombre de columna invalido '" + c.name + "'");
        }
        if (!seen.insert(c.name).second) {
            throw DataError("tabla " + schema.name + ": la columna " + c.name + " esta repetida");
        }
    }
    if (schema.reference_column >= schema.columns.size()) {
        throw DataError("tabla " + schema.name + ": la columna de referencia no existe");
    }
    if (schema.columns[schema.reference_column].type != ColumnType::kInteger) {
        throw DataError("tabla " + schema.name + ": la columna de referencia " +
                        schema.columns[schema.reference_column].name + " debe ser entera");
    }
}

/*
 * Implementacion: valida cada estructura, revisa nombres de tabla repetidos
 * y recorre las filas comprobando ancho, tipos y que la columna de
 * referencia no repita valores.
 */
void validate(const Database& database) {
    std::set<std::string> names;
    for (const Table& table : database.tables) {
        const TableSchema& schema = table.schema;
        validate_schema(schema);
        if (!names.insert(schema.name).second) {
            throw DataError("la tabla " + schema.name + " aparece dos veces");
        }
        std::unordered_set<std::int64_t> references;
        references.reserve(table.rows.size());
        for (std::size_t r = 0; r < table.rows.size(); ++r) {
            const Row& row = table.rows[r];
            if (row.size() != schema.columns.size()) {
                throw DataError(describe_row(schema, r, row) + ": tiene " + std::to_string(row.size()) +
                                " celdas y la tabla tiene " + std::to_string(schema.columns.size()) +
                                " columnas");
            }
            for (std::size_t c = 0; c < row.size(); ++c) {
                const bool is_int = std::holds_alternative<std::int64_t>(row[c]);
                if (is_int != (schema.columns[c].type == ColumnType::kInteger)) {
                    throw DataError(describe_row(schema, r, row) + ": la columna " + schema.columns[c].name +
                                    " debe ser de tipo " + column_type_name(schema.columns[c].type));
                }
            }
            const std::int64_t ref = std::get<std::int64_t>(row[schema.reference_column]);
            if (!references.insert(ref).second) {
                throw DataError("tabla " + schema.name + ": el valor " + std::to_string(ref) +
                                " se repite en la columna de referencia " +
                                schema.columns[schema.reference_column].name);
            }
        }
    }
}

/*
 * Implementacion: std::get_if para no lanzar std::bad_variant_access.
 */
std::int64_t integer_cell(const Cell& cell) {
    if (const auto* v = std::get_if<std::int64_t>(&cell)) return *v;
    throw DataError("se esperaba un valor entero y la celda es de texto");
}

}  // namespace tt::db

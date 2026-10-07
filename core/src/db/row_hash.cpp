// row_hash.cpp

#include "tt/db/row_hash.hpp"

#include <string>

namespace tt::db {

namespace {

// Etiqueta de dominio del hash de fila (ver la tabla de etiquetas en
// tt/merkle/tree.hpp).
constexpr std::uint8_t kTagRow = 0x10;

// Marca de tipo de cada celda.
constexpr std::uint8_t kCellInteger = 0x00;
constexpr std::uint8_t kCellText = 0x01;

}  // namespace

/*
 * Implementacion: revisa la forma de la fila y agrega al SHA-256 la
 * etiqueta, el nombre de la tabla, el numero de columnas y cada celda con
 * su marca de tipo.
 */
crypto::Digest row_hash(const TableSchema& schema, const Row& row) {
    if (row.size() != schema.columns.size()) {
        throw DataError("tabla " + schema.name + ": la fila tiene " + std::to_string(row.size()) +
                        " celdas y la tabla " + std::to_string(schema.columns.size()) + " columnas");
    }
    crypto::Sha256 h;
    h.u8(kTagRow).u64(schema.name.size()).bytes(schema.name.data(), schema.name.size());
    h.u64(row.size());
    for (std::size_t i = 0; i < row.size(); ++i) {
        const bool integer_column = schema.columns[i].type == ColumnType::kInteger;
        if (const auto* v = std::get_if<std::int64_t>(&row[i]); v != nullptr && integer_column) {
            h.u8(kCellInteger).i64(*v);
        } else if (const auto* s = std::get_if<std::string>(&row[i]); s != nullptr && !integer_column) {
            h.u8(kCellText).u64(s->size()).bytes(s->data(), s->size());
        } else {
            throw DataError("tabla " + schema.name + ": la celda de la columna " + schema.columns[i].name +
                            " no es de tipo " + column_type_name(schema.columns[i].type));
        }
    }
    return h.finish();
}

}  // namespace tt::db

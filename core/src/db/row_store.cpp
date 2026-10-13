// row_store.cpp

#include "tt/db/row_store.hpp"

#include <utility>

namespace tt::db {

/*
 * Implementacion: valida la base (asi las referencias son unicas y enteras)
 * y arma un indice referencia -> fila por cada tabla.
 */
MemoryRowStore::MemoryRowStore(Database database) : database_(std::move(database)) {
    validate(database_);
    index_.resize(database_.tables.size());
    for (std::size_t t = 0; t < database_.tables.size(); ++t) {
        const Table& table = database_.tables[t];
        const std::size_t ref = table.schema.reference_column;
        for (std::size_t r = 0; r < table.rows.size(); ++r) {
            index_[t].emplace(integer_cell(table.rows[r][ref]), r);
        }
    }
}

/*
 * Implementacion: localiza la tabla por nombre, exige la misma estructura y
 * copia cada fila pedida en el orden de `references`.
 */
std::vector<Row> MemoryRowStore::fetch_rows(const TableSchema& schema, const std::vector<std::int64_t>& references) {
    for (std::size_t t = 0; t < database_.tables.size(); ++t) {
        const Table& table = database_.tables[t];
        if (table.schema.name != schema.name) continue;
        if (!(table.schema == schema)) throw DataError("tabla " + schema.name + ": la estructura no coincide");
        std::vector<Row> rows;
        rows.reserve(references.size());
        for (std::int64_t ref : references) {
            const auto it = index_[t].find(ref);
            if (it == index_[t].end()) {
                throw DataError("tabla " + schema.name + ": no existe la fila con " +
                                schema.columns[schema.reference_column].name + " = " + std::to_string(ref));
            }
            rows.push_back(table.rows[it->second]);
        }
        return rows;
    }
    throw DataError("la tabla " + schema.name + " no existe");
}

}  // namespace tt::db

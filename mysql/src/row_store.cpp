// row_store.cpp

#include "tt/mysql/row_store.hpp"

#include <algorithm>
#include <unordered_map>

#include "tt/mysql/database_source.hpp"

namespace tt::mysql {

namespace {

// Identificadores por consulta IN (...).
constexpr std::size_t kBatch = 1000;

}  // namespace

/*
 * Implementacion: abre la transaccion consistente, pide las filas por
 * bloques, las indexa por su referencia y las devuelve en el orden pedido.
 */
std::vector<db::Row> DatabaseRowStore::fetch_rows(const db::TableSchema& schema,
                                                  const std::vector<std::int64_t>& references) {
    if (references.empty()) return {};
    const std::size_t ref = schema.reference_column;
    const std::string& ref_name = schema.columns[ref].name;
    const std::string select = select_columns_sql(schema) + " WHERE " + quoted_identifier(ref_name) + " IN (";

    Connection c(config_);
    c.execute("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ");
    c.execute("START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY");
    std::unordered_map<std::int64_t, db::Row> found;
    found.reserve(references.size());
    for (std::size_t start = 0; start < references.size(); start += kBatch) {
        const std::size_t end = std::min(references.size(), start + kBatch);
        std::string sql = select;
        for (std::size_t i = start; i < end; ++i) sql += (i > start ? "," : "") + std::to_string(references[i]);
        sql += ")";
        c.query(sql, [&](const std::vector<Field>& f) {
            db::Row row = row_from_fields(schema, f);
            const std::int64_t key = std::get<std::int64_t>(row[ref]);
            found.emplace(key, std::move(row));
        });
    }
    c.execute("COMMIT");

    std::vector<db::Row> rows;
    rows.reserve(references.size());
    for (std::int64_t r : references) {
        auto it = found.find(r);
        if (it == found.end()) {
            throw db::DataError("tabla " + schema.name + ": la fila con " + ref_name + " = " + std::to_string(r) +
                                " no existe en la base (se borro despues de Setup?)");
        }
        rows.push_back(std::move(it->second));
        found.erase(it);
    }
    return rows;
}

}  // namespace tt::mysql

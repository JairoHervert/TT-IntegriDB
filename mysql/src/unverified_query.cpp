// unverified_query.cpp

#include "tt/mysql/unverified_query.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace tt::mysql {

namespace {

using Clock = std::chrono::steady_clock;

/*
 * Convierte el texto de una celda al tipo de su columna. Los enteros deben
 * ocupar todo el texto; AVG llega como DECIMAL y se lee como real.
 *
 * Errores:
 *   Lanza DatabaseError si el texto no es del tipo esperado.
 */
query::ResultValue convert_cell(const Field& field, const query::UnverifiedColumn& column) {
    if (!field) return std::monostate{};
    const std::string& text = *field;
    try {
        std::size_t used = 0;
        if (column.type == query::UnverifiedType::kInteger) {
            const long long v = std::stoll(text, &used);
            if (used == text.size()) return static_cast<std::int64_t>(v);
        } else if (column.type == query::UnverifiedType::kReal) {
            const double v = std::stod(text, &used);
            if (used == text.size() && std::isfinite(v)) return v;
        } else {
            return text;
        }
    } catch (const std::exception&) {
        // Se informa abajo con el nombre de la columna.
    }
    throw DatabaseError("MySQL devolvio un valor inesperado en la columna " + column.name);
}

}  // namespace

/*
 * Implementacion: igual que la referencia SQL directa, la lectura se hace
 * en una instantanea consistente y de solo lectura; la codificacion se
 * mide aparte.
 */
UnverifiedSqlResult run_unverified_query(const ConnectionConfig& config, const query::UnverifiedQuery& query,
                                         std::size_t max_rows) {
    query::UnverifiedResult result;
    result.catalog_id = query.catalog_id;
    result.columns = query::unverified_columns(query);

    UnverifiedSqlResult out;
    const auto sql_start = Clock::now();
    Connection connection(config);
    connection.execute("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ");
    connection.execute("START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY");
    connection.query(query::unverified_sql(query), [&](const std::vector<Field>& fields) {
        if (fields.size() != result.columns.size()) {
            throw DatabaseError("las columnas SQL no coinciden con " + query.catalog_id);
        }
        ++out.rows;
        if (result.rows.size() >= max_rows) return;
        std::vector<query::ResultValue> row;
        row.reserve(fields.size());
        for (std::size_t i = 0; i < fields.size(); ++i) row.push_back(convert_cell(fields[i], result.columns[i]));
        result.rows.push_back(std::move(row));
    });
    connection.execute("COMMIT");
    result.row_count = out.rows;
    out.sql_seconds = std::chrono::duration<double>(Clock::now() - sql_start).count();

    const auto encode_start = Clock::now();
    out.bytes = query::encode_unverified_result(result);
    out.encoding_seconds = std::chrono::duration<double>(Clock::now() - encode_start).count();
    return out;
}

}  // namespace tt::mysql

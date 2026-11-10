// direct_query.cpp

#include "tt/mysql/direct_query.hpp"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tt/db/table.hpp"
#include "tt/query/verifier_service.hpp"  // ResultValue y ResultTable

namespace tt::mysql {

namespace {

using Clock = std::chrono::steady_clock;

/*
 * Agrega un texto JSON escapado, sin interpretar el contenido de MySQL
 * como sintaxis o como codigo ejecutable del navegador.
 */
void write_text(std::string& out, const std::string& text) {
    out += '"';
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
                    out += escaped;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

/*
 * Escribe una celda tipada de forma equivalente al resultado de Verify.
 */
void write_value(std::string& out, const query::ResultValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        out += "null";
    } else if (const auto* number = std::get_if<std::int64_t>(&value)) {
        out += std::to_string(*number);
    } else if (const auto* real = std::get_if<double>(&value)) {
        char text[40];
        std::snprintf(text, sizeof text, "%.15g", *real);
        out += text;
    } else {
        write_text(out, std::get<std::string>(value));
    }
}

/*
 * Consulta la estructura del digest, no la informacion que declare el
 * servidor SQL. Para un JOIN las columnas llevan el prefijo de la tabla.
 */
void add_schema(const scheme::DatabaseDigest& digest, const std::string& table, bool qualified,
                std::vector<std::string>& names, std::vector<db::ColumnType>& types) {
    const scheme::TableDigest* found = digest.find_table(table);
    if (found == nullptr) throw std::runtime_error("no hay estructura de " + table + " en delta");
    for (const db::Column& column : found->schema.columns) {
        names.push_back(qualified ? table + "." + column.name : column.name);
        types.push_back(column.type);
    }
}

/*
 * Adapta los nombres y tipos del resultado SQL a los que publica Verify.
 */
void result_schema(const scheme::DatabaseDigest& digest, const query::ParsedQuery& query,
                   std::vector<std::string>& names, std::vector<db::ColumnType>& types, bool& scalar,
                   bool& average) {
    scalar = false;
    average = false;
    switch (query::query_kind(query)) {
        case query::QueryKind::kJoinCourse:
            add_schema(digest, "academic_record", true, names, types);
            add_schema(digest, "course", true, names, types);
            return;
        case query::QueryKind::kJoinProgram:
            add_schema(digest, "student", true, names, types);
            add_schema(digest, "program", true, names, types);
            return;
        case query::QueryKind::kSelection:
        case query::QueryKind::kMultiRange:
            add_schema(digest, query.query.table, false, names, types);
            return;
        case query::QueryKind::kAverage:
            names.push_back("AVG(grade)");
            average = scalar = true;
            break;
        case query::QueryKind::kCount:
            names.push_back("COUNT(*)");
            scalar = true;
            break;
        case query::QueryKind::kMinimum:
            names.push_back("MIN(grade)");
            scalar = true;
            break;
        case query::QueryKind::kSumJoinCourse:
            names.push_back("SUM(course.credits_tenths)");
            scalar = true;
            break;
    }
    types.push_back(db::ColumnType::kInteger);
}

/*
 * Codifica el resultado completo en la misma forma JSON que la interfaz
 * entrega para una respuesta verificada (sin la prueba criptografica).
 */
std::string result_json(const query::ParsedQuery& query, const std::vector<std::string>& names,
                        const query::ResultTable& table, std::size_t count) {
    std::string out;
    out.reserve(256 + table.rows.size() * names.size() * 12);
    out += "{\"outcome\":\"direct\",\"unverified\":true,\"catalog_id\":";
    write_text(out, query.catalog_id);
    out += ",\"result\":{\"columns\":[";
    for (std::size_t c = 0; c < names.size(); ++c) {
        if (c) out += ',';
        write_text(out, names[c]);
    }
    out += "],\"row_count\":" + std::to_string(count);
    out += ",\"truncated\":";
    out += (count > table.rows.size() ? "true" : "false");
    out += ",\"rows\":[";
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        if (r) out += ',';
        out += '[';
        for (std::size_t c = 0; c < table.rows[r].size(); ++c) {
            if (c) out += ',';
            write_value(out, table.rows[r][c]);
        }
        out += ']';
    }
    out += "]}}";
    return out;
}

}  // namespace

/*
 * Implementacion: SQL se ejecuta en el servidor que tiene acceso a MySQL.
 * Primero se completa la lectura; despues se mide por separado el JSON.
 * La sentencia proviene de to_sql y solo usa consultas tipadas del catalogo.
 */
DirectSqlResult run_direct_query(const ConnectionConfig& config, const scheme::DatabaseDigest& digest,
                                 const query::ParsedQuery& query, std::size_t max_rows) {
    std::vector<std::string> names;
    std::vector<db::ColumnType> types;
    bool scalar = false;
    bool average = false;
    result_schema(digest, query, names, types, scalar, average);

    DirectSqlResult out;
    query::ResultTable table;
    const auto sql_start = Clock::now();
    Connection connection(config);
    // Cada respuesta usa una instantanea de lectura consistente.
    connection.execute("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ");
    connection.execute("START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY");
    connection.query(query::to_sql(query), [&](const std::vector<Field>& fields) {
        if (fields.size() != names.size()) throw DatabaseError("las columnas SQL no coinciden con delta");
        ++out.rows;
        if (table.rows.size() >= max_rows) return;
        std::vector<query::ResultValue> row;
        row.reserve(fields.size());
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (!fields[i]) row.emplace_back(std::monostate{});
            else if (average) row.emplace_back(std::stod(*fields[i]));
            else if (scalar || types[i] == db::ColumnType::kInteger) row.emplace_back(static_cast<std::int64_t>(std::stoll(*fields[i])));
            else row.emplace_back(*fields[i]);
        }
        table.rows.push_back(std::move(row));
    });
    connection.execute("COMMIT");
    out.sql_seconds = std::chrono::duration<double>(Clock::now() - sql_start).count();
    const auto encode_start = Clock::now();
    out.json = result_json(query, names, table, out.rows);
    out.encoding_seconds = std::chrono::duration<double>(Clock::now() - encode_start).count();
    return out;
}

}  // namespace tt::mysql

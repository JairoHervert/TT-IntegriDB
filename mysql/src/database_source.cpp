// database_source.cpp

#include "tt/mysql/database_source.hpp"

#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tt::mysql {

namespace {

// Lo que information_schema dice de una tabla mientras se arma su
// estructura.
struct PendingTable {
    db::TableSchema schema;
    std::vector<std::string> primary_key;
};

/*
 * Devuelve el texto de un campo que no puede ser NULL (los de
 * information_schema que se consultan aqui).
 *
 * Errores:
 *   Lanza DatabaseError si el campo llega como NULL.
 */
const std::string& required(const Field& field, const char* what) {
    if (!field) throw DatabaseError(std::string("information_schema devolvio NULL en ") + what);
    return *field;
}

/*
 * Descubre las tablas base, sus columnas y su llave primaria, y arma la
 * estructura de cada una.
 *
 * Errores:
 *   Lanza db::DataError si una tabla no cumple el modelo del esquema.
 */
std::vector<db::TableSchema> read_schemas(Connection& c) {
    std::map<std::string, PendingTable> tables;
    c.query("SELECT TABLE_NAME, ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() "
            "AND TABLE_TYPE = 'BASE TABLE' ORDER BY TABLE_NAME",
            [&](const std::vector<Field>& f) {
                const std::string& name = required(f[0], "TABLE_NAME");
                if (!db::is_valid_identifier(name)) throw db::DataError("nombre de tabla no soportado: '" + name + "'");
                if (!f[1] || *f[1] != "InnoDB") {
                    throw db::DataError("tabla " + name + ": usa el motor " + f[1].value_or("desconocido") +
                                        "; la lectura con instantanea consistente necesita InnoDB");
                }
                tables[name].schema.name = name;
            });

    c.query("SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE, IS_NULLABLE, COLUMN_TYPE FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() ORDER BY TABLE_NAME, ORDINAL_POSITION",
            [&](const std::vector<Field>& f) {
                const auto it = tables.find(required(f[0], "TABLE_NAME"));
                if (it == tables.end()) return;  // columnas de una vista
                const std::string& table = it->first;
                const std::string& column = required(f[1], "COLUMN_NAME");
                const std::string& type = required(f[2], "DATA_TYPE");
                if (!db::is_valid_identifier(column)) {
                    throw db::DataError("tabla " + table + ": nombre de columna no soportado '" + column + "'");
                }
                const std::optional<db::ColumnType> mapped = column_type_from_mysql(type);
                if (!mapped) {
                    throw db::DataError("tabla " + table + ", columna " + column + ": el tipo " + type +
                                        " no esta soportado (solo enteros y texto; ver el modelo de datos)");
                }
                if (required(f[3], "IS_NULLABLE") != "NO") {
                    throw db::DataError("tabla " + table + ", columna " + column +
                                        ": admite NULL y el esquema necesita columnas NOT NULL");
                }
                if (required(f[4], "COLUMN_TYPE").find("zerofill") != std::string::npos) {
                    throw db::DataError("tabla " + table + ", columna " + column +
                                        ": ZEROFILL no esta soportado (cambia el texto de los enteros)");
                }
                it->second.schema.columns.push_back(db::Column{column, *mapped});
            });

    c.query("SELECT TABLE_NAME, COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE "
            "WHERE TABLE_SCHEMA = DATABASE() AND CONSTRAINT_NAME = 'PRIMARY' ORDER BY TABLE_NAME, ORDINAL_POSITION",
            [&](const std::vector<Field>& f) {
                const auto it = tables.find(required(f[0], "TABLE_NAME"));
                if (it != tables.end()) it->second.primary_key.push_back(required(f[1], "COLUMN_NAME"));
            });

    std::vector<db::TableSchema> schemas;
    for (auto& [name, pending] : tables) {
        if (pending.primary_key.size() != 1) {
            throw db::DataError("tabla " + name + ": el esquema necesita una llave primaria de una sola columna "
                                "entera (la columna de referencia)");
        }
        const std::optional<std::size_t> ref = pending.schema.column_index(pending.primary_key[0]);
        if (!ref) throw db::DataError("tabla " + name + ": no se encontro la columna de la llave primaria");
        pending.schema.reference_column = *ref;
        db::validate_schema(pending.schema);
        schemas.push_back(std::move(pending.schema));
    }
    return schemas;
}

/*
 * Lee todas las filas de una tabla, ordenadas por su columna de referencia,
 * y convierte cada campo segun el tipo de su columna.
 *
 * Errores:
 *   Lanza db::DataError si llega un NULL o un entero invalido.
 */
db::Table read_table(Connection& c, const db::TableSchema& schema) {
    const std::string sql = select_columns_sql(schema) + " ORDER BY " +
                            quoted_identifier(schema.columns[schema.reference_column].name);

    db::Table table{schema, {}};
    c.query(sql, [&](const std::vector<Field>& f) { table.rows.push_back(row_from_fields(schema, f)); });
    return table;
}

}  // namespace

/*
 * Implementacion: conecta, abre la transaccion de solo lectura con
 * instantanea consistente, lee estructuras y filas, y cierra la
 * transaccion. Si algo falla, la conexion se cierra y MySQL descarta la
 * transaccion.
 */
db::Database DatabaseSource::read() {
    Connection c(config_);
    c.execute("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ");
    c.execute("START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY");
    db::Database database;
    for (const db::TableSchema& schema : read_schemas(c)) database.tables.push_back(read_table(c, schema));
    c.execute("COMMIT");
    return database;
}

/*
 * Implementacion: pasa el nombre a minusculas (MySQL lo envia en
 * minusculas, pero no cuesta nada no depender de eso) y lo compara con la
 * lista de tipos enteros y de texto.
 */
std::optional<db::ColumnType> column_type_from_mysql(const std::string& data_type) {
    std::string lower = data_type;
    for (char& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    for (const char* t : {"tinyint", "smallint", "mediumint", "int", "bigint"}) {
        if (lower == t) return db::ColumnType::kInteger;
    }
    for (const char* t : {"char", "varchar", "tinytext", "text", "mediumtext", "longtext"}) {
        if (lower == t) return db::ColumnType::kText;
    }
    return std::nullopt;
}

/*
 * Implementacion: revisa el formato, acumula el valor absoluto como
 * entero sin signo y comprueba que quepa en int64 (el menor negativo,
 * -2^63, tiene un valor absoluto una unidad mayor que el maximo positivo).
 */
bool parse_mysql_integer(const std::string& text, std::int64_t& out) {
    const bool negative = !text.empty() && text[0] == '-';
    const std::string digits = negative ? text.substr(1) : text;
    if (digits.empty() || digits.size() > 19 || (digits.size() > 1 && digits[0] == '0') || (negative && digits == "0")) {
        return false;
    }
    std::uint64_t v = 0;
    for (char ch : digits) {
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + static_cast<std::uint64_t>(ch - '0');  // 19 digitos no desbordan u64
    }
    const std::uint64_t max_positive = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (negative) {
        if (v > max_positive + 1) return false;
        out = v == max_positive + 1 ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(v);
    } else {
        if (v > max_positive) return false;
        out = static_cast<std::int64_t>(v);
    }
    return true;
}

/*
 * Implementacion: comillas invertidas alrededor del nombre.
 */
std::string quoted_identifier(const std::string& identifier) { return "`" + identifier + "`"; }

/*
 * Implementacion: lista las columnas en el orden de la estructura.
 */
std::string select_columns_sql(const db::TableSchema& schema) {
    std::string sql = "SELECT ";
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        sql += (i ? ", " : "") + quoted_identifier(schema.columns[i].name);
    }
    return sql + " FROM " + quoted_identifier(schema.name);
}

/*
 * Implementacion: exige un campo por columna, rechaza NULL y convierte los
 * enteros con parse_mysql_integer; el texto se copia tal cual.
 */
db::Row row_from_fields(const db::TableSchema& schema, const std::vector<Field>& fields) {
    if (fields.size() != schema.columns.size()) {
        throw db::DataError("tabla " + schema.name + ": MySQL devolvio " + std::to_string(fields.size()) +
                            " columnas y la estructura tiene " + std::to_string(schema.columns.size()));
    }
    db::Row row;
    row.reserve(fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const db::Column& column = schema.columns[i];
        if (!fields[i]) throw db::DataError("tabla " + schema.name + ", columna " + column.name + ": valor NULL");
        if (column.type == db::ColumnType::kText) {
            row.emplace_back(*fields[i]);
            continue;
        }
        std::int64_t v = 0;
        if (!parse_mysql_integer(*fields[i], v)) {
            throw db::DataError("tabla " + schema.name + ", columna " + column.name + ": el valor '" + *fields[i] +
                                "' no es un entero de 64 bits");
        }
        row.emplace_back(v);
    }
    return row;
}

}  // namespace tt::mysql

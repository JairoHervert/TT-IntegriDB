// table.hpp
//
// Modelo relacional minimo que usa el esquema: tablas con columnas enteras
// o de texto, filas y la columna de referencia de cada tabla.
//
// Es la forma en que el Propietario de datos recibe la base D antes de
// Setup, sin importar de donde venga (MySQL, memoria en las pruebas...).
// Las reglas vienen del modelo de datos del reporte:
//   - Toda tabla tiene una columna de referencia: entera y con valores
//     unicos (la llave primaria sustituta). Identifica cada fila en los
//     conjuntos autenticados y ordena las hojas del arbol de Merkle de filas.
//   - Solo las columnas enteras pueden participar en un AIT. Las de texto se
//     protegen unicamente con el hash de la fila.
//   - No hay valores NULL: el modelo declara todas las columnas NOT NULL.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace tt::db {

// Tipo de una columna, tal como lo entiende el esquema.
enum class ColumnType : std::uint8_t { kInteger = 0, kText = 1 };

// Una columna: nombre y tipo.
struct Column {
    std::string name;
    ColumnType type = ColumnType::kInteger;

    /*
     * Compara nombre y tipo.
     */
    bool operator==(const Column& o) const { return name == o.name && type == o.type; }
};

// Valor de una celda: entero de 64 bits o texto (bytes UTF-8 tal cual).
using Cell = std::variant<std::int64_t, std::string>;

// Una fila: una celda por columna, en el orden de las columnas.
using Row = std::vector<Cell>;

// Estructura de una tabla.
struct TableSchema {
    std::string name;
    std::vector<Column> columns;
    std::size_t reference_column = 0;  // indice de la columna de referencia

    /*
     * Busca una columna por nombre.
     *
     * Devuelve:
     *   Su indice, o std::nullopt si la tabla no tiene esa columna.
     */
    std::optional<std::size_t> column_index(const std::string& column) const;

    /*
     * Compara nombre, columnas y columna de referencia.
     */
    bool operator==(const TableSchema& o) const {
        return name == o.name && columns == o.columns && reference_column == o.reference_column;
    }
};

// Una tabla con sus filas (en cualquier orden).
struct Table {
    TableSchema schema;
    std::vector<Row> rows;
};

// La base de datos D completa.
struct Database {
    std::vector<Table> tables;

    /*
     * Busca una tabla por nombre.
     *
     * Devuelve:
     *   Puntero a la tabla, o nullptr si no existe.
     */
    const Table* find(const std::string& table) const;
};

// Error en los datos o en la estructura de la base (el mensaje dice cual).
class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Indica si un nombre de tabla o columna es aceptable para el esquema:
 * de 1 a 64 caracteres, solo letras ASCII, digitos y guion bajo, y no
 * empieza con digito. Asi los nombres se pueden escribir en el digest y en
 * consultas SQL sin comillas especiales ni ambiguedades.
 */
bool is_valid_identifier(const std::string& name);

/*
 * Devuelve el nombre de un tipo de columna tal como aparece en el digest:
 * "integer" o "text".
 */
const char* column_type_name(ColumnType type);

/*
 * Convierte "integer" o "text" en el tipo de columna correspondiente.
 *
 * Devuelve:
 *   El tipo, o std::nullopt si el texto no es ninguno de los dos.
 */
std::optional<ColumnType> parse_column_type(const std::string& name);

/*
 * Revisa que la estructura de una tabla cumpla las reglas del esquema:
 * nombre valido, al menos una columna, nombres de columna validos y sin
 * repetir, y columna de referencia existente y entera.
 *
 * Errores:
 *   Lanza DataError con la descripcion del problema.
 */
void validate_schema(const TableSchema& schema);

/*
 * Revisa una base completa antes de Setup: la estructura de cada tabla
 * (validate_schema), que no haya dos tablas con el mismo nombre, que cada
 * fila tenga una celda por columna con el tipo correcto y que la columna de
 * referencia no repita valores.
 *
 * Errores:
 *   Lanza DataError con la tabla, la fila y el problema encontrado.
 */
void validate(const Database& database);

/*
 * Devuelve el valor entero de una celda.
 *
 * Errores:
 *   Lanza DataError si la celda es de texto.
 */
std::int64_t integer_cell(const Cell& cell);

}  // namespace tt::db

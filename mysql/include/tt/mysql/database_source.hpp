// database_source.hpp
//
// Adaptador MySQL del puerto db::Source: lee la base D completa para que el
// propietario ejecute Setup.
//
// Como la lee:
//   1. Abre una transaccion de solo lectura con instantanea consistente
//      (START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY). Todas las
//      tablas se leen del mismo estado aunque alguien escriba al mismo
//      tiempo, asi delta describe un estado que realmente existio.
//   2. Descubre la estructura en information_schema: tablas base, columnas
//      en su orden y llave primaria. La llave primaria es la columna de
//      referencia, y debe ser una sola columna entera. Todas las tablas
//      deben ser InnoDB (la instantanea consistente solo cubre InnoDB).
//   3. Traduce los tipos: TINYINT, SMALLINT, MEDIUMINT, INT y BIGINT son
//      enteros; CHAR, VARCHAR y los TEXT son texto. Cualquier otro tipo
//      (DECIMAL, DATE, FLOAT, BLOB...) se rechaza con un mensaje claro, igual
//      que las columnas que admiten NULL: el modelo del reporte no los usa.
//   4. Lee las filas de cada tabla ordenadas por la llave primaria.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "tt/db/source.hpp"
#include "tt/mysql/connection.hpp"

namespace tt::mysql {

// Lee la base de MySQL.
class DatabaseSource final : public db::Source {
public:
    /*
     * Guarda los datos de conexion; la conexion se abre en cada read().
     */
    explicit DatabaseSource(ConnectionConfig config) : config_(std::move(config)) {}

    /*
     * Lee estructura y filas de todas las tablas base de la base configurada
     * (ver los pasos arriba).
     *
     * Errores:
     *   Lanza DatabaseError si MySQL falla y db::DataError si una tabla no
     *   cumple el modelo (motor distinto de InnoDB, tipo no soportado, NULL,
     *   ZEROFILL, sin llave primaria entera de una sola columna, entero
     *   fuera de rango).
     */
    db::Database read() override;

    /*
     * Describe el destino (sin la contrasena).
     */
    std::string description() const override { return config_.describe(); }

private:
    ConnectionConfig config_;
};

/*
 * Traduce el DATA_TYPE de information_schema al tipo del esquema.
 *
 * Devuelve:
 *   kInteger para los enteros, kText para CHAR/VARCHAR/TEXT, o
 *   std::nullopt si el tipo no esta soportado.
 */
std::optional<db::ColumnType> column_type_from_mysql(const std::string& data_type);

/*
 * Convierte el texto de un entero tal como lo envia MySQL ("-12", "0",
 * "2024630001") en int64. Es estricto: solo un signo menos opcional y
 * digitos, sin ceros a la izquierda y dentro del rango de 64 bits (un
 * BIGINT UNSIGNED grande no cabe y se rechaza).
 *
 * Devuelve:
 *   true y el valor en `out` si el texto es valido.
 */
bool parse_mysql_integer(const std::string& text, std::int64_t& out);

}  // namespace tt::mysql

// row_store.hpp
//
// Adaptador MySQL del puerto db::RowStore: el modulo probador obtiene de la
// base D las filas que indica el AIT.
//
// Como las lee:
//   - Dentro de una transaccion de solo lectura con instantanea
//     consistente, igual que Setup: todas las filas de una respuesta salen
//     del mismo estado de la base.
//   - Pide las columnas en el orden de la estructura publicada en delta
//     (no en el orden en que esten en MySQL), con
//     SELECT ... WHERE <referencia> IN (...), en bloques de 1000
//     identificadores para no armar consultas enormes.
//   - Devuelve las filas en el orden en que se pidieron.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tt/db/row_store.hpp"
#include "tt/mysql/connection.hpp"

namespace tt::mysql {

// Lee filas de MySQL por su columna de referencia.
class DatabaseRowStore final : public db::RowStore {
public:
    /*
     * Guarda los datos de conexion; la conexion se abre en cada fetch_rows().
     */
    explicit DatabaseRowStore(ConnectionConfig config) : config_(std::move(config)) {}

    /*
     * Lee las filas pedidas (ver arriba).
     *
     * Errores:
     *   Lanza DatabaseError si MySQL falla y db::DataError si alguna
     *   referencia no existe en la tabla (por ejemplo, la fila se borro
     *   despues de Setup) o si una fila no se puede convertir.
     */
    std::vector<db::Row> fetch_rows(const db::TableSchema& schema, const std::vector<std::int64_t>& references) override;

    /*
     * Describe el destino (sin la contrasena).
     */
    std::string description() const override { return config_.describe(); }

private:
    ConnectionConfig config_;
};

}  // namespace tt::mysql

// row_store.hpp
//
// Puerto por el que el modulo probador obtiene las filas de la base D para
// armar una respuesta. Igual que db::Source (la lectura completa que usa el
// propietario en Setup), el nucleo no sabe nada de MySQL: el adaptador real
// vive en tt_mysql y las pruebas usan MemoryRowStore.
//
// Por que el probador toma las filas de D y no de D~: D~ solo guarda, por
// cada fila, su referencia y su hash (lo necesario para las pruebas de
// Merkle), no la fila completa. Ademas, el servidor no es confiable: si
// alguien modifica D despues de Setup, el probador enviara la fila
// modificada y el verificador la rechazara porque su hash ya no coincide
// con la raiz publicada en delta. Eso es justo lo que el esquema debe
// detectar.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "tt/db/table.hpp"

namespace tt::db {

// Origen de filas por su valor en la columna de referencia.
class RowStore {
public:
    /*
     * Destructor virtual: los adaptadores se usan a traves de RowStore&.
     */
    virtual ~RowStore() = default;

    /*
     * Devuelve las filas de una tabla cuyos valores en la columna de
     * referencia son `references`, en ese mismo orden. Las celdas siguen el
     * orden y los tipos de las columnas de `schema` (la estructura que
     * publica delta).
     *
     * Parametros:
     *   schema      estructura de la tabla (nombre, columnas, referencia)
     *   references  valores de la columna de referencia, sin repetir
     *
     * Devuelve:
     *   Una fila por referencia, en el mismo orden.
     *
     * Errores:
     *   Lanza DataError si alguna referencia no existe o si una fila no se
     *   puede convertir a la estructura; cada adaptador puede lanzar ademas
     *   sus propios errores (por ejemplo, de conexion).
     */
    virtual std::vector<Row> fetch_rows(const TableSchema& schema, const std::vector<std::int64_t>& references) = 0;

    /*
     * Describe el origen para los mensajes al usuario.
     */
    virtual std::string description() const = 0;
};

// Adaptador en memoria: busca las filas en una base ya armada. Lo usan las
// pruebas (por ejemplo, con una base alterada despues de Setup).
class MemoryRowStore final : public RowStore {
public:
    /*
     * Indexa las filas de cada tabla por su columna de referencia.
     *
     * Errores:
     *   Lanza DataError si la base no es valida (ver db::validate).
     */
    explicit MemoryRowStore(Database database);

    /*
     * Busca cada referencia en el indice de la tabla. La estructura pedida
     * debe ser exactamente la de la tabla guardada.
     *
     * Errores:
     *   Lanza DataError si la tabla no existe, si su estructura es otra o si
     *   falta alguna referencia.
     */
    std::vector<Row> fetch_rows(const TableSchema& schema, const std::vector<std::int64_t>& references) override;

    /*
     * Devuelve "base en memoria".
     */
    std::string description() const override { return "base en memoria"; }

private:
    Database database_;
    // por tabla (indice en database_.tables): referencia -> indice de la fila
    std::vector<std::unordered_map<std::int64_t, std::size_t>> index_;
};

}  // namespace tt::db

// unverified_query.hpp
//
// Ejecucion en MySQL de las consultas no verificables N1 a N5 (ver
// tt/query/unverified.hpp). El resultado se entrega en formato TTUNV, sin
// prueba: el verificador lo muestra como NO verificado.

#pragma once

#include <cstddef>
#include <vector>

#include "tt/mysql/connection.hpp"
#include "tt/query/unverified.hpp"

namespace tt::mysql {

// Resultado codificado y tiempos del servidor (informativos).
struct UnverifiedSqlResult {
    std::vector<unsigned char> bytes;  // resultado en TTUNV
    std::size_t rows = 0;              // filas que devolvio MySQL
    double sql_seconds = 0;            // conexion + SQL + lectura y conversion
    double encoding_seconds = 0;       // codificacion TTUNV
};

/*
 * Ejecuta la plantilla SQL de la consulta en una transaccion de solo
 * lectura y codifica el resultado.
 *
 * Parametros:
 *   config    conexion exclusiva para esta ejecucion
 *   query     consulta validada con parse_unverified_query
 *   max_rows  filas maximas que se codifican (row_count conserva el total)
 *
 * Devuelve:
 *   El resultado en TTUNV y los tiempos.
 *
 * Errores:
 *   Deja pasar los errores de MySQL y lanza DatabaseError si una fila no
 *   tiene las columnas o los tipos esperados.
 */
UnverifiedSqlResult run_unverified_query(const ConnectionConfig& config, const query::UnverifiedQuery& query,
                                         std::size_t max_rows = 200000);

}  // namespace tt::mysql

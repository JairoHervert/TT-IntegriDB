// direct_query.hpp
//
// Referencia experimental de SQL sin verificacion criptografica. Ejecuta
// exclusivamente las formas del catalogo que construye query::to_sql; nunca
// recibe una sentencia SQL arbitraria desde el navegador.
//
// El resultado no tiene prueba de integridad y NO debe presentarse como
// verificado. La medicion SQL incluye conexion y lectura de todas las filas;
// la codificacion JSON se mide por separado.

#pragma once

#include <cstddef>
#include <string>

#include "tt/mysql/connection.hpp"
#include "tt/query/query.hpp"
#include "tt/scheme/digest.hpp"

namespace tt::mysql {

struct DirectSqlResult {
    std::string json;             // resultado de la consulta sin autenticacion
    std::size_t rows = 0;         // filas que devolvio MySQL
    double sql_seconds = 0;       // conexion + SQL + lectura y conversion
    double encoding_seconds = 0;  // codificacion JSON, fuera del cronometro SQL
};

/*
 * Ejecuta la consulta normalizada en MySQL (sin AIT, Merkle ni ASO).
 *
 * Parametros:
 *   config     conexion exclusiva para esta ejecucion
 *   digest     estructura de columnas publicada para el caso de estudio
 *   query      consulta leida mediante parse_query (sin SQL libre)
 *   max_rows   limite de filas que se envian al navegador
 *
 * Devuelve:
 *   JSON de resultado no verificado y tiempos del servidor.
 *
 * Errores:
 *   Deja pasar errores de MySQL y de conversion; nunca entrega una
 *   respuesta parcial como si hubiera sido una consulta exitosa.
 */
DirectSqlResult run_direct_query(const ConnectionConfig& config, const scheme::DatabaseDigest& digest,
                                 const query::ParsedQuery& query, std::size_t max_rows = 200000);

}  // namespace tt::mysql

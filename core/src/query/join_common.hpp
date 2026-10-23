#pragma once

// Nucleo compartido por C8 y C9. Este archivo es interno a core/src/query:
// las interfaces publicas, comandos CLI y formatos binarios se conservan.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "tt/ait/range_proof.hpp"
#include "tt/aso/intersection.hpp"
#include "tt/db/row_store.hpp"
#include "tt/query/selection.hpp"
#include "tt/scheme/authenticated_db.hpp"

namespace tt::query {

/*
 * Especifica un JOIN muchos-a-uno entre dos columnas enteras del mismo nombre.
 * La columna derecha debe ser unica en las filas coincidentes (clave primaria
 * en C8 y C9). No es una interfaz para JOIN arbitrarios o muchos-a-muchos.
 *
 * Campos:
 *   query_id    identificador del catalogo (C8 o C9)
 *   left_table  tabla con posibles claves de union repetidas
 *   right_table tabla con claves de union unicas
 *   key_column  columna usada por los AIT (i,i) y el JOIN
 */
struct JoinConfig {
    const char* query_id;
    const char* left_table;
    const char* right_table;
    const char* key_column;
};

/*
 * Estructura interna comun para calcular las respuestas C8 y C9.
 * El campo rows[0] corresponde a left_table y rows[1] a right_table.
 */
struct JoinCoreResponse {
    crypto::Digest state_id{};
    std::array<ait::RangeProof, 2> ranges;
    std::array<aso::SplitWitness, 2> splits;
    std::array<aso::DisjointWitness, 2> disjoint;
    std::array<std::vector<RowProof>, 2> rows;
};

struct JoinCoreAnswer {
    JoinCoreResponse response;
    std::size_t stale_rows = 0;
};

/*
 * Vista de solo lectura a una respuesta C8/C9 sin copiar filas ni testigos.
 * El Verifier recibe exclusivamente datos publicos y la respuesta no fiable.
 */
struct JoinResponseView {
    const crypto::Digest& state_id;
    const std::array<ait::RangeProof, 2>& ranges;
    const std::array<aso::SplitWitness, 2>& splits;
    const std::array<aso::DisjointWitness, 2>& disjoint;
    const std::vector<RowProof>& left_rows;
    const std::vector<RowProof>& right_rows;
};

struct JoinCoreVerification {
    bool accepted = false;
    std::string reason;
    std::array<std::vector<db::Row>, 2> rows;
    std::size_t joined_rows = 0;
};

/*
 * Construye una prueba de JOIN muchos-a-uno usando dos AIT, testigos Split,
 * certificados Bezout y rutas Merkle; no conoce la clave secreta del Owner.
 *
 * Parametros:
 *   adb     estructuras autenticadas recibidas por el Prover
 *   digest  compromiso del estado contra el cual se construyen las pruebas
 *   pk      clave publica ASO correspondiente al estado
 *   store   recuperador de filas completas de la base SQL
 *   config  tablas y columna de union para C8 o C9
 *
 * Devuelve:
 *   Respuesta y numero de filas que difieren del estado de Setup.
 *
 * Errores:
 *   Lanza UnsupportedQuery si falta algun AIT; ProverError si falta la
 *   tabla o una referencia, y propaga errores de MySQL/criptografia.
 */
JoinCoreAnswer answer_join_common(const scheme::AuthenticatedDatabase& adb,
                                  const scheme::DatabaseDigest& digest,
                                  const aso::PublicKey& pk, db::RowStore& store,
                                  const JoinConfig& config);

/*
 * Comprueba pertenencia y completitud del JOIN sin consultar MySQL ni D~.
 * Exige que ninguna referencia aparezca dos veces y que la clave derecha
 * no se repita entre las filas participantes del JOIN.
 *
 * Parametros:
 *   digest    compromiso autenticado proporcionado por el Owner
 *   pk        clave publica ASO; no se utiliza la clave secreta
 *   response  vista de la respuesta binaria no confiable
 *   config    tablas y columna esperadas para la consulta solicitada
 *
 * Devuelve:
 *   accepted=true solo cuando las pruebas AIT, ASO y Merkle son validas.
 *
 * Errores:
 *   Devuelve accepted=false y la causa en reason para respuestas invalidas.
 */
JoinCoreVerification verify_join_common(const scheme::DatabaseDigest& digest,
                                        const aso::PublicKey& pk,
                                        const JoinResponseView& response,
                                        const JoinConfig& config);

}  // namespace tt::query

// prover_service.hpp
//
// Punto de entrada unico del modulo probador: recibe cualquier consulta del
// catalogo (C1 a C14 y "range") y devuelve la respuesta (R, pi) ya
// codificada en el formato TTRSP.
//
// Antes, cada comando del probador elegia por su cuenta la funcion ProofGen
// de cada tipo de consulta y la forma de guardar la respuesta. Al
// concentrarlo aqui, el comando que escribe un archivo (tt_prover answer) y
// el servidor de red (tt_prover serve) responden exactamente igual: los dos
// solo deciden a donde van los bytes.
//
// El servicio no conoce la red ni MySQL: recibe el estado autenticado, la
// clave publica y el puerto db::RowStore por el que se leen las filas.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "tt/aso/keys.hpp"
#include "tt/db/row_store.hpp"
#include "tt/query/query.hpp"
#include "tt/query/selection.hpp"  // ProverError
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"

namespace tt::query {

// Lo que el probador tiene a la mano para responder. Los apuntadores no son
// duenos de los objetos; quien crea el contexto los mantiene vivos.
struct ProverContext {
    const scheme::AuthenticatedDatabase* database = nullptr;  // D~
    const scheme::DatabaseDigest* digest = nullptr;           // delta
    const aso::PublicKey* public_key = nullptr;  // necesaria solo de C7 a C14 (puede ser nula)
    db::RowStore* store = nullptr;               // filas de D (puede ser nulo si no se leen filas)
};

// Respuesta del probador a una consulta.
struct ProverReply {
    std::vector<unsigned char> bytes;  // (R, pi) en formato TTRSP, listo para enviarse
    std::string summary;               // resumen para la bitacora del probador
    std::size_t stale_rows = 0;        // filas de D que cambiaron despues de Setup
    // Mediciones operativas, no autenticadas por el protocolo.
    double generation_seconds = 0;  // ProofGen, incluidas lecturas de D (no solo CPU)
    double encoding_seconds = 0;    // codificacion binaria TTRSP
    double row_fetch_seconds = 0;   // fetch_rows() de D: conexion y lectura incluidas
    std::size_t row_fetch_calls = 0; // numero de llamadas al origen de filas
    std::size_t row_fetch_rows = 0;  // filas solicitadas al origen de datos
    // Solo C8, C9 y C14: duraciones no solapadas de operaciones concretas.
    bool detailed_stages = false;
    double ait_seconds = 0;     // consultas de rango a estructuras AIT
    double merkle_seconds = 0;  // hashes y caminos de inclusion de filas
    double aso_seconds = 0;     // Split, Bezout y conversiones necesarias
};

/*
 * Indica si la consulta necesita la clave publica para generar su prueba
 * (testigos del bloque ASO). Las consultas de seleccion y de rango en una
 * columna no la necesitan.
 *
 * Parametros:
 *   query  consulta ya leida
 *
 * Devuelve:
 *   true para C7 a C14.
 */
bool needs_public_key(const ParsedQuery& query);

/*
 * Indica si para responder la consulta se leen filas de la base D. Las
 * consultas de promedio y de conteo (C10 a C12) solo usan D~.
 *
 * Parametros:
 *   query  consulta ya leida
 *
 * Devuelve:
 *   true si la respuesta incluye filas de D.
 */
bool reads_rows(const ParsedQuery& query);

/*
 * ProofGen de cualquier consulta del catalogo: elige el procedimiento segun
 * query_kind, genera la respuesta y la codifica en TTRSP.
 *
 * Parametros:
 *   context  estado autenticado, clave publica y origen de filas
 *   query    consulta ya leida (parse_query)
 *
 * Devuelve:
 *   Los bytes de la respuesta, un resumen y el numero de filas que ya no son
 *   las de Setup (el verificador rechazara la respuesta si es mayor que 0).
 *
 * Errores:
 *   Lanza UnsupportedQuery si el estado autenticado no tiene los arboles que
 *   requiere la consulta; ProverError si falta la clave publica o el origen
 *   de filas que la consulta necesita, o si D~ es inconsistente; y deja
 *   pasar los errores del origen de filas (por ejemplo, MySQL caido).
 */
ProverReply answer_query(const ProverContext& context, const ParsedQuery& query);

}  // namespace tt::query

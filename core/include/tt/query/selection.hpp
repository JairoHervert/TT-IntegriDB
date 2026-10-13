// selection.hpp
//
// ProofGen y Verify para las consultas de seleccion y de rango sobre una
// columna (C1 a C6): lo que hace el modulo probador para responder y lo que
// hace el modulo verificador para aceptar o rechazar la respuesta.
//
// La respuesta (R, pi) contiene:
//   - el id del estado (delta) sobre el que se respondio;
//   - la consulta que responde;
//   - la prueba de RangeCover del AIT (columna -> referencia): el arbol
//     podado de tt/ait/range_proof.hpp;
//   - cada fila del resultado, completa, con su prueba de inclusion en el
//     arbol de Merkle de su tabla. Las filas van en el orden del AIT (por
//     la columna filtrada).
//
// El verificador acepta solo si, en este orden:
//   1. la respuesta es del estado y de la consulta que el tiene;
//   2. la prueba del AIT reconstruye la raiz publicada en delta
//      (verify_range): asi obtiene, autenticados, los nodos de cobertura
//      del rango, el intervalo [min, max] de claves de cada uno y cuantas
//      filas hay debajo de cada uno;
//   3. hay exactamente tantas filas como suman esos contadores;
//   4. cada fila es autentica: su hash, con su prueba de Merkle, reconstruye
//      la raiz de su tabla en delta; y ninguna fila se repite;
//   5. a cada nodo de cobertura le tocan, en orden, tantas filas como dice
//      su contador, y todas tienen la columna filtrada dentro del intervalo
//      [min, max] de ese nodo.
//
// Por que esto basta para que no falte ni sobre ninguna fila: las hojas del
// AIT estan ordenadas por clave, asi que el subarbol de un nodo contiene
// TODAS las claves de la tabla que caen en su intervalo [min, max], y su
// contador es el numero de filas con esas claves. Si el servidor entrega
// ese numero de filas distintas, autenticas (Merkle) y con la clave en el
// intervalo, son exactamente esas filas: no hay otras. Y la forma canonica
// de la prueba del AIT garantiza que los nodos de cobertura abarcan todo
// [lo, hi]. Esto funciona porque nuestro hash de nodo compromete el
// contador y el intervalo (en el IntegriDB original no los compromete).
//
// Comprobacion con el ASO (opcional, Check::kWithAccumulators): ademas, para
// cada nodo de cobertura, el acumulador de los identificadores de sus filas,
// calculado con la clave publica, debe ser el acumulador autenticado del
// nodo:  acc({r^{-1} : r en esas filas}) == g1^{e_u}.  Es el procedimiento
// del articulo para rangos. Con los contadores autenticados es redundante
// (por el argumento de arriba) y es lo mas costoso de la verificacion (una
// combinacion de |R| + 1 potencias de pk), por eso no se usa por defecto; se
// conserva para comparar tiempos y como verificacion "literal" del
// articulo. El ASO si es indispensable en las consultas que combinan
// conjuntos (rango en dos columnas, JOIN, SUM), donde el resultado ya no
// coincide con nodos completos.

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt/ait/range_proof.hpp"
#include "tt/aso/keys.hpp"
#include "tt/db/row_store.hpp"
#include "tt/db/table.hpp"
#include "tt/merkle/tree.hpp"
#include "tt/query/query.hpp"
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"

namespace tt::query {

// Una fila del resultado con su prueba de inclusion.
struct RowProof {
    db::Row row;
    merkle::InclusionProof inclusion;
};

// Respuesta (R, pi) a una consulta de seleccion o de rango.
struct SelectionResponse {
    crypto::Digest state_id{};    // id del digest delta sobre el que se respondio
    RangeQuery query;             // la consulta que responde
    ait::RangeProof range;        // prueba de RangeCover del AIT
    std::vector<RowProof> rows;   // filas del resultado, en el orden del AIT
};

// Lo que produce el probador, con datos para informar al operador.
struct SelectionAnswer {
    SelectionResponse response;
    scheme::TreeSpec tree;        // AIT usado
    std::size_t stale_rows = 0;   // filas de D cuyo hash ya no es el de D~ (D cambio despues de Setup)
};

// El probador no puede responder (D~ no corresponde a delta, o le falta
// una fila que el AIT indica).
class ProverError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * ProofGen para seleccion y rango (lado del probador). Ejecuta RangeCover
 * en el AIT de la consulta, toma de la base D las filas de los
 * identificadores obtenidos y agrega la prueba de Merkle de cada una.
 *
 * Si una fila de D ya no tiene el hash que tenia en Setup (alguien la
 * modifico), igual se envia tal como esta en D: el probador no oculta
 * nada y el verificador la rechazara. stale_rows lo informa para que el
 * operador del servidor se entere.
 *
 * Parametros:
 *   adb     base autenticada D~ (cargada y comprobada contra delta)
 *   digest  digest delta vigente
 *   query   consulta a responder
 *   store   origen de las filas de D (MySQL o memoria)
 *
 * Devuelve:
 *   La respuesta lista para enviarse.
 *
 * Errores:
 *   Lanza UnsupportedQuery si delta no tiene el AIT de la consulta,
 *   ProverError si D~ no tiene el arbol o una fila, y deja pasar los
 *   errores del origen de filas (por ejemplo, una fila borrada de D).
 */
SelectionAnswer answer_selection(const scheme::AuthenticatedDatabase& adb, const scheme::DatabaseDigest& digest,
                                 const RangeQuery& query, db::RowStore& store);

// Que tanto revisa verify_selection (ver arriba).
enum class Check {
    kCounts,            // pasos 1 a 5 (por defecto)
    kWithAccumulators,  // pasos 1 a 5 y ademas el acumulador de cada nodo de cobertura
};

// Tiempo de cada etapa de la verificacion, en segundos (RF16).
struct VerificationTimes {
    double range = 0;         // prueba del AIT (solo hashes)
    double rows = 0;          // hash y prueba de Merkle de cada fila, y paso 5
    double accumulators = 0;  // acumuladores de los nodos de cobertura (solo con kWithAccumulators)
};

// Resultado de la verificacion.
struct SelectionVerification {
    bool accepted = false;
    std::string reason;              // por que se rechazo (vacio si se acepto)
    std::vector<db::Row> rows;       // el resultado verificado (vacio si se rechazo)
    std::size_t cover_nodes = 0;     // nodos de cobertura del rango
    VerificationTimes times;
};

/*
 * Verify para seleccion y rango (lado del verificador): aplica los pasos
 * descritos arriba. Nunca lanza por algo que envie el servidor: todo
 * problema de la respuesta se devuelve como rechazo con su motivo, y en ese
 * caso no se entrega ninguna fila.
 *
 * Parametros:
 *   digest    digest delta vigente (lo tiene el verificador)
 *   pk        clave publica (su huella debe ser la de delta; lo revisa
 *             quien la carga). Solo se usa con Check::kWithAccumulators.
 *   asked     la consulta que hizo el verificador
 *   response  la respuesta del servidor
 *   check     que tanto revisar (por defecto, pasos 1 a 5)
 *
 * Devuelve:
 *   accepted = true y las filas del resultado, o accepted = false y el
 *   motivo.
 */
SelectionVerification verify_selection(const scheme::DatabaseDigest& digest, const aso::PublicKey& pk,
                                       const RangeQuery& asked, const SelectionResponse& response,
                                       Check check = Check::kCounts);

}  // namespace tt::query

#pragma once

// C8: JOIN completo academic_record.course_id = course.course_id.
// Los AIT (i,i) autentican los multiconjuntos de claves; dos separaciones
// ASO y dos certificados de coprimalidad evitan omitir coincidencias.
// Las filas de ambas tablas se autentican por Merkle, sin confiar en D~.

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

struct JoinCourseResponse {
    crypto::Digest state_id{};
    std::array<ait::RangeProof, 2> ranges;          // academic_record, course: rango total
    std::array<aso::SplitWitness, 2> splits;        // multiconjuntos no publicados
    std::array<aso::DisjointWitness, 2> disjoint;   // resto_record x course; resto_course x record
    std::vector<RowProof> record_rows;
    std::vector<RowProof> course_rows;
};

struct JoinCourseAnswer {
    JoinCourseResponse response;
    std::size_t stale_rows = 0;
};

struct JoinCourseVerification {
    bool accepted = false;
    std::string reason;
    std::vector<db::Row> record_rows;
    std::vector<db::Row> course_rows;
    std::size_t joined_rows = 0;
};

/*
 * Genera la respuesta de C8 a partir de ambos AIT y filas de MySQL.
 * Cada valor de course_id se conserva con su multiplicidad en academic_record.
 *
 * Parametros:
 *   adb     base autenticada que recibe el Prover (nunca contiene sk)
 *   digest  compromiso del estado publicado durante Setup
 *   pk      clave publica ASO para construir los testigos bilineales
 *   store   origen de las filas completas, separado del nucleo criptografico
 *
 * Devuelve:
 *   Filas academic_record/course autenticables y testigos ASO de completitud.
 *
 * Errores:
 *   Lanza UnsupportedQuery cuando falta un AIT, y ProverError cuando no
 *   coincide D~ con las filas disponibles; propaga errores de ASO y MySQL.
 */
JoinCourseAnswer answer_join_course(const scheme::AuthenticatedDatabase& adb,
                                    const scheme::DatabaseDigest& digest,
                                    const aso::PublicKey& pk, db::RowStore& store);

/*
 * Comprueba C8 usando exclusivamente pk, delta y la respuesta recibida.
 * Nunca consulta MySQL, D~, la clave secreta ni archivos del Owner.
 *
 * Parametros:
 *   digest    compromiso de estado autorizado para esta consulta
 *   pk        clave publica ASO correspondiente a digest
 *   response  filas y testigos no confiables enviados por el Prover
 *
 * Devuelve:
 *   accepted=true solo si no faltan ni sobran filas en el JOIN, y todas
 *   estan autenticadas por sus respectivos arboles de Merkle.
 *
 * Errores:
 *   Comunica fallos de AIT, Merkle, ASO o de formato en reason con
 *   accepted=false; no lanza excepciones para respuestas manipuladas.
 */
JoinCourseVerification verify_join_course(const scheme::DatabaseDigest& digest,
                                          const aso::PublicKey& pk,
                                          const JoinCourseResponse& response);

}  // namespace tt::query

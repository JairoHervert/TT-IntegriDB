#pragma once

// C14: composicion C7 (filtrado) + JOIN autenticado sobre course + SUM local.
// No se autentica un SUM producido por el servidor: el Verifier lo calcula
// exclusivamente a partir de filas autenticadas y conservando multiplicidades.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tt/query/multirange.hpp"

namespace tt::query {

struct SumJoinCourseResponse {
    crypto::Digest state_id{};
    std::int64_t student_id = 0;
    MultiRangeResponse filtered;                 // C7: student_id = ? AND grade >= 6
    ait::RangeProof course_range;                // AIT course(course_id -> course_id), rango total
    aso::SplitWitness course_complement;        // cursos NO usados por el JOIN filtrado
    aso::DisjointWitness no_missing_courses;    // complemento_course coprimo con claves filtradas
    std::vector<RowProof> course_rows;           // una fila por course_id coincidente (Merkle)
};

struct SumJoinCourseAnswer {
    SumJoinCourseResponse response;
    std::size_t stale_rows = 0;
};

struct SumJoinCourseVerification {
    bool accepted = false;
    std::string reason;
    std::optional<std::int64_t> credits_tenths;  // NULL si no hay filas del INNER JOIN
    std::size_t filtered_rows = 0;
    std::size_t joined_rows = 0;
    std::size_t course_rows = 0;
};

/*
 * Construye los dos predicados de C14 para reutilizar la interseccion de C7.
 * La consulta SQL original no se modifica ni se ejecuta como JOIN en MySQL.
 *
 * Parametros:
 *   student_id  identificador del alumno solicitado
 *
 * Devuelve:
 *   Filtros student_id = ? y grade >= 6 sobre academic_record.
 */
MultiRangeQuery c14_filter(std::int64_t student_id);

/*
 * Genera la respuesta C14 reutilizando la seleccion completa de C7 y un
 * JOIN filtrado autenticado contra course. Conserva multiplicidades SQL;
 * no ejecuta el JOIN original ni incluye una SUM calculada por el Prover.
 *
 * Parametros:
 *   adb         base autenticada D~ con AIT y arboles Merkle de Setup
 *   digest      compromiso de estado publicado por el Owner
 *   pk          clave publica ASO para construir Split y Bezout
 *   student_id  alumno cuyos creditos aprobados se consultan
 *   store       origen de filas completas (MySQL o memoria)
 *
 * Devuelve:
 *   Pruebas de C7, AIT course, ASO y Merkle; contador de filas obsoletas.
 *
 * Errores:
 *   Lanza UnsupportedQuery si faltan AIT requeridos, ProverError si D~
 *   no corresponde a las filas y propaga errores de ASO o RowStore.
 */
SumJoinCourseAnswer answer_sum_join_course(const scheme::AuthenticatedDatabase& adb,
                                           const scheme::DatabaseDigest& digest,
                                           const aso::PublicKey& pk,
                                           std::int64_t student_id, db::RowStore& store);

/*
 * Comprueba C14 con pk, delta y una respuesta no confiable del Prover.
 * Verifica C7, completitud del JOIN por Split/Bezout y filas por Merkle;
 * calcula SUM localmente respetando las multiplicidades de academic_record.
 * No consulta MySQL, D~ ni la clave secreta del Owner.
 *
 * Parametros:
 *   digest      compromiso de estado autorizado para C14
 *   pk          clave publica ASO correspondiente a digest
 *   student_id  parametro de la consulta solicitado por el cliente
 *   response    filas y testigos no confiables recibidos del Prover
 *
 * Devuelve:
 *   accepted=true y creditos en decimas; creditos_tenths es NULL cuando
 *   el INNER JOIN no tiene filas. Incluye contadores auxiliares.
 *
 * Errores:
 *   Rechaza pruebas alteradas en reason con accepted=false, sin propagar
 *   las excepciones causadas por la respuesta recibida.
 */
SumJoinCourseVerification verify_sum_join_course(const scheme::DatabaseDigest& digest,
                                                const aso::PublicKey& pk,
                                                std::int64_t student_id,
                                                const SumJoinCourseResponse& response);

}  // namespace tt::query

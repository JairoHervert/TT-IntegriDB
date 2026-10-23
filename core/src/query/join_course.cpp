#include "tt/query/join_course.hpp"

#include <utility>

#include "join_common.hpp"

namespace tt::query {
namespace {

// C8: el lado derecho tiene clave course_id unica.
constexpr JoinConfig kConfig{"C8", "academic_record", "course", "course_id"};

}  // namespace

/*
 * Adapta la respuesta comun a la interfaz publica de C8 sin modificar
 * los campos ni la codificacion binaria del mensaje TTRSP existente.
 *
 * Parametros:
 *   adb     estado autenticado del Prover
 *   digest  compromiso de estado de Setup
 *   pk      clave publica ASO
 *   store   proveedor de filas completas desde MySQL
 *
 * Devuelve:
 *   Respuesta y testigos criptograficos especificos de C8.
 *
 * Errores:
 *   Propaga las excepciones del nucleo comun y del RowStore.
 */
JoinCourseAnswer answer_join_course(const scheme::AuthenticatedDatabase& adb,
                                    const scheme::DatabaseDigest& digest,
                                    const aso::PublicKey& pk, db::RowStore& store) {
    auto common = answer_join_common(adb, digest, pk, store, kConfig);
    JoinCourseAnswer out;
    out.stale_rows = common.stale_rows;
    out.response.state_id = common.response.state_id;
    out.response.ranges = std::move(common.response.ranges);
    out.response.splits = std::move(common.response.splits);
    out.response.disjoint = std::move(common.response.disjoint);
    out.response.record_rows = std::move(common.response.rows[0]);
    out.response.course_rows = std::move(common.response.rows[1]);
    return out;
}

/*
 * Reutiliza la verificacion comun de AIT, Split, Bezout y Merkle.
 * Mantiene los nombres publicos de los resultados y no copia pruebas.
 *
 * Parametros:
 *   digest    compromiso autorizado del Owner
 *   pk        clave publica ASO
 *   response  respuesta no fiable del Prover
 *
 * Devuelve:
 *   Veredicto, filas verificadas y total del JOIN C8.
 *
 * Errores:
 *   Los errores de verificacion se devuelven en reason.
 */
JoinCourseVerification verify_join_course(const scheme::DatabaseDigest& digest,
                                          const aso::PublicKey& pk,
                                          const JoinCourseResponse& response) {
    const JoinResponseView view{response.state_id, response.ranges, response.splits,
                                response.disjoint, response.record_rows, response.course_rows};
    auto common = verify_join_common(digest, pk, view, kConfig);
    JoinCourseVerification out;
    out.accepted = common.accepted;
    out.reason = std::move(common.reason);
    out.record_rows = std::move(common.rows[0]);
    out.course_rows = std::move(common.rows[1]);
    out.joined_rows = common.joined_rows;
    return out;
}

}  // namespace tt::query

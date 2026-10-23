#include "tt/query/join_program.hpp"

#include <utility>

#include "join_common.hpp"

namespace tt::query {
namespace {

// C9: el lado derecho tiene clave program_id unica.
constexpr JoinConfig kConfig{"C9", "student", "program", "program_id"};

}  // namespace

/*
 * Adapta la respuesta comun a la interfaz publica de C9 sin modificar
 * los campos ni la codificacion binaria del mensaje TTRSP existente.
 *
 * Parametros:
 *   adb     estado autenticado del Prover
 *   digest  compromiso de estado de Setup
 *   pk      clave publica ASO
 *   store   proveedor de filas completas desde MySQL
 *
 * Devuelve:
 *   Respuesta y testigos criptograficos especificos de C9.
 *
 * Errores:
 *   Propaga las excepciones del nucleo comun y del RowStore.
 */
JoinProgramAnswer answer_join_program(const scheme::AuthenticatedDatabase& adb,
                                      const scheme::DatabaseDigest& digest,
                                      const aso::PublicKey& pk, db::RowStore& store) {
    auto common = answer_join_common(adb, digest, pk, store, kConfig);
    JoinProgramAnswer out;
    out.stale_rows = common.stale_rows;
    out.response.state_id = common.response.state_id;
    out.response.ranges = std::move(common.response.ranges);
    out.response.splits = std::move(common.response.splits);
    out.response.disjoint = std::move(common.response.disjoint);
    out.response.student_rows = std::move(common.response.rows[0]);
    out.response.program_rows = std::move(common.response.rows[1]);
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
 *   Veredicto, filas verificadas y total del JOIN C9.
 *
 * Errores:
 *   Los errores de verificacion se devuelven en reason.
 */
JoinProgramVerification verify_join_program(const scheme::DatabaseDigest& digest,
                                            const aso::PublicKey& pk,
                                            const JoinProgramResponse& response) {
    const JoinResponseView view{response.state_id, response.ranges, response.splits,
                                response.disjoint, response.student_rows, response.program_rows};
    auto common = verify_join_common(digest, pk, view, kConfig);
    JoinProgramVerification out;
    out.accepted = common.accepted;
    out.reason = std::move(common.reason);
    out.student_rows = std::move(common.rows[0]);
    out.program_rows = std::move(common.rows[1]);
    out.joined_rows = common.joined_rows;
    return out;
}

}  // namespace tt::query

#include "tt/query/minimum.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace tt::query {
namespace {
/* El limite inferior representa todos los enteros <= candidato. */
MultiRangeQuery minimum_range(std::int64_t student_id, std::int64_t candidate) {
    return {{"academic_record", "student_id", student_id, student_id},
            {"academic_record", "grade", std::numeric_limits<std::int64_t>::min(), candidate}};
}
}  // namespace

MinimumAnswer answer_minimum(const scheme::AuthenticatedDatabase& adb,
                             const scheme::DatabaseDigest& digest,
                             const aso::PublicKey& pk, std::int64_t student_id,
                             db::RowStore& store) {
    // Este AIT ya lo necesita C10. Solo permite seleccionar el candidato:
    // NO forma parte de la evidencia que el cliente acepta para verificar MIN.
    const scheme::TreeSpec grade_values{"academic_record", "student_id", "grade"};
    if (!digest.find_tree(grade_values)) {
        throw UnsupportedQuery("C13 requiere AIT existente " + grade_values.label());
    }
    const auto* tree = adb.find_tree(grade_values);
    if (!tree) throw ProverError("D~ no contiene " + grade_values.label());
    const auto values = tree->range(student_id, student_id).values;
    MinimumAnswer answer;
    if (!values.empty()) answer.minimum = *std::min_element(values.begin(), values.end());

    // Para el caso vacio se usa el menor entero como candidato tecnico.
    // El cliente no acepta MIN=NULL a menos que el AIT autentique que el
    // alumno NO tiene filas; si hay filas y no se entregan, rechaza.
    const auto candidate = answer.minimum.value_or(std::numeric_limits<std::int64_t>::min());
    answer.range_answer = answer_multirange(adb, digest, pk, minimum_range(student_id, candidate), store);
    if (answer.minimum && answer.range_answer.response.rows.empty()) {
        throw ProverError("C13: minimo presente sin filas en el intervalo autenticado");
    }
    return answer;
}

MinimumVerification verify_minimum(const scheme::DatabaseDigest& digest,
                                   const aso::PublicKey& pk, std::int64_t student_id,
                                   const MultiRangeResponse& response) {
    MinimumVerification out;
    try {
        const auto& q = response.query;
        if (!(q.first == RangeQuery{"academic_record", "student_id", student_id, student_id}) ||
            q.second.table != "academic_record" || q.second.column != "grade" ||
            q.second.lo != std::numeric_limits<std::int64_t>::min()) {
            throw std::runtime_error("C13: consulta/rango de respuesta inconsistente");
        }
        const auto validated = verify_multirange(digest, pk, q, response);
        if (!validated.accepted) throw std::runtime_error(validated.reason);
        out.witnesses_rows = validated.rows.size();
        if (validated.rows.empty()) {
            // El resultado NULL es correcto exclusivamente si no hay
            // registros de este alumno, NO solo si grade<=candidato es vacio.
            const auto spec = selection_tree(q.first, digest);
            const auto* tree = digest.find_tree(spec);
            if (!tree) throw std::runtime_error("C13: no existe AIT de alumno");
            const auto student = ait::verify_range(tree->root, student_id, student_id,
                                                    response.ranges[0]);
            if (!student.accepted || student.count != 0) {
                throw std::runtime_error("C13: un alumno con filas no puede tener MIN=NULL");
            }
        } else {
            const auto* tab = digest.find_table("academic_record");
            if (!tab) throw std::runtime_error("C13: tabla ausente del digest");
            const auto grade_col = tab->schema.column_index("grade");
            if (!grade_col) throw std::runtime_error("C13: columna grade ausente");
            for (const auto& row : validated.rows) {
                if (std::get<std::int64_t>(row[*grade_col]) != q.second.hi) {
                    throw std::runtime_error("C13: existe una calificacion menor que el MIN propuesto");
                }
            }
            out.minimum = q.second.hi;
        }
        out.accepted = true;
    } catch (const std::exception& e) {
        out.accepted = false;
        out.minimum.reset();
        out.reason = e.what();
    }
    return out;
}
}  // namespace tt::query

#include "tt/query/average.hpp"

#include <limits>
#include <stdexcept>

#include "tt/query/query.hpp"

namespace tt::query {
namespace {
/*
 * Devuelve la especificacion del AIT ya construido para C10.
 *
 * Devuelve:
 *   Arbol academic_record(student_id -> grade).
 */
scheme::TreeSpec average_tree() { return {"academic_record", "student_id", "grade"}; }
}
AverageResponse answer_average(const scheme::AuthenticatedDatabase& adb,
                               const scheme::DatabaseDigest& digest,
                               const aso::PublicKey& pk, std::int64_t student_id) {
    const auto spec = average_tree();
    if (!digest.find_tree(spec)) throw UnsupportedQuery("C10: no existe AIT " + spec.label());
    const auto* tree = adb.find_tree(spec);
    if (!tree) throw UnsupportedQuery("C10: D~ no contiene " + spec.label());
    auto ranged = tree->range(student_id, student_id);
    AverageResponse response;
    response.state_id = digest.id();
    response.student_id = student_id;
    response.count = ranged.values.size();
    response.range = std::move(ranged.proof);
    if (response.count == 0) return response;
    if (response.count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()/10))
        throw std::overflow_error("C10: SUM excede int64");
    for (const auto grade : ranged.values) {
        if (grade < 5 || grade > 10) throw std::runtime_error("C10: grade fuera del esquema 5..10");
        response.sum += grade;
    }
    response.proof = aso::prove_sum(ranged.values, pk);
    return response;
}
AverageVerification verify_average(const scheme::DatabaseDigest& digest,
                                   const aso::PublicKey& pk, std::int64_t student_id,
                                   const AverageResponse& response) {
    AverageVerification out;
    try {
        if (response.state_id != digest.id() || response.student_id != student_id)
            throw std::runtime_error("C10: estado o alumno incorrecto");
        const auto spec = average_tree();
        const auto* tree = digest.find_tree(spec);
        if (!tree) throw std::runtime_error("C10: AIT no autenticado");
        const auto checked = ait::verify_range(tree->root, student_id, student_id, response.range);
        if (!checked.accepted) throw std::runtime_error("C10: RangeCover invalido: " + checked.reason);
        if (checked.count != response.count) throw std::runtime_error("C10: COUNT no coincide con el AIT");
        if (!response.count) {
            if (!checked.cover.empty() || response.sum != 0)
                throw std::runtime_error("C10: resultado vacio invalido");
        } else {
            if (checked.cover.size() != 1 || !checked.cover[0].header.is_leaf)
                throw std::runtime_error("C10: se esperaba una hoja del alumno");
            if (checked.cover[0].header.split_key != student_id)
                throw std::runtime_error("C10: hoja de otro alumno");
            if (!aso::verify_sum(checked.cover[0].payload.acc_g1,
                                 response.count, response.sum, response.proof, pk))
                throw std::runtime_error("C10: testigos de SUM invalidos");
        }
        out.accepted = true;
        out.count = response.count;
        out.sum = response.sum;
    } catch (const std::exception& e) {
        out.accepted = false;
        out.reason = e.what();
    }
    return out;
}
} // namespace tt::query

#include "tt/query/count_intersection.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "tt/ait/node.hpp"
#include "tt/query/selection.hpp"

namespace tt::query {
namespace {
class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Valida las formas del catalogo sin relajar C12 al agregar C11.
 * C11: student_id = ? AND grade < 6; limite inferior INT64_MIN.
 * C12: student_id = ? AND evaluation_type = ?.
 *
 * Parametros:
 *   q  consulta a validar como C11 o C12
 *
 * Errores:
 *   Lanza UnsupportedQuery ante una consulta que no pertenezca a C11/C12.
 */
void check_shape(const CountQuery& q) {
    const bool common = q.first.table == "academic_record" &&
        q.second.table == "academic_record" &&
        q.first.column == "student_id" && q.first.lo == q.first.hi;
    const bool c11 = q.catalog_id == "C11" && q.second.column == "grade" &&
        q.second.lo == std::numeric_limits<std::int64_t>::min() && q.second.hi == 5;
    const bool c12 = q.catalog_id == "C12" && q.second.column == "evaluation_type" &&
        q.second.lo == q.second.hi;
    if (!common || (!c11 && !c12)) {
        throw UnsupportedQuery("COUNT requiere C11 (student_id = ? AND grade < 6) o "
                               "C12 (student_id = ? AND evaluation_type = ?)");
    }
}

/*
 * Convierte identificadores de fila a elementos del campo usados por ASO.
 *
 * Parametros:
 *   refs  identificadores de fila a representar
 *
 * Devuelve:
 *   FieldSet con un elemento por identificador, conservando el orden.
 */
aso::FieldSet elements(const std::vector<std::int64_t>& refs) {
    aso::FieldSet out;
    out.elements.reserve(refs.size());
    for (const auto id : refs) out.elements.push_back(ait::value_to_element(id));
    return out;
}

/*
 * Extrae de la prueba los nodos que cubren el intervalo consultado.
 *
 * Parametros:
 *   proof  prueba del RangeCover suministrada por el AIT
 *
 * Devuelve:
 *   Nodos de cobertura en el orden de la prueba.
 */
std::vector<ait::CoverNode> covers(const ait::RangeProof& proof) {
    std::vector<ait::CoverNode> out;
    for (const auto& entry : proof.entries) {
        if (entry.kind == ait::EntryKind::kCover)
            out.push_back({entry.header, entry.payload});
    }
    return out;
}

/*
 * Comprueba que las particiones declaradas suman exactamente los ID
 * publicados, sin duplicados ni omisiones.
 *
 * Parametros:
 *   parts     referencias publicadas por cada nodo de cobertura
 *   expected  lista global ordenada de referencias anunciadas
 *
 * Devuelve:
 *   true si los ID de parts coinciden una sola vez con expected.
 */
bool same_unique_references(const std::vector<std::vector<std::int64_t>>& parts,
                            const std::vector<std::int64_t>& expected) {
    std::vector<std::int64_t> all;
    for (const auto& part : parts) all.insert(all.end(), part.begin(), part.end());
    if (all.size() != expected.size()) return false;
    std::sort(all.begin(), all.end());
    return std::adjacent_find(all.begin(), all.end()) == all.end() && all == expected;
}
}  // namespace

CountAnswer answer_count(const scheme::AuthenticatedDatabase& adb,
                         const scheme::DatabaseDigest& digest,
                         const aso::PublicKey& pk, const CountQuery& q) {
    check_shape(q);
    CountAnswer answer;
    auto& response = answer.response;
    response.state_id = digest.id();
    response.query = q;
    const std::array<RangeQuery, 2> predicates{q.first, q.second};
    std::array<ait::RangeAnswer, 2> ranged;
    for (int k = 0; k < 2; ++k) {
        answer.trees[k] = selection_tree(predicates[k], digest);
        const auto* tree = adb.find_tree(answer.trees[k]);
        if (!tree) throw ProverError("D~ no contiene AIT " + answer.trees[k].label());
        ranged[k] = tree->range(predicates[k].lo, predicates[k].hi);
        answer.input_counts[k] = ranged[k].values.size();
        response.ranges[k] = std::move(ranged[k].proof);
    }

    const std::unordered_set<std::int64_t> right(ranged[1].values.begin(), ranged[1].values.end());
    for (std::int64_t id : ranged[0].values) {
        if (right.count(id)) response.references.push_back(id);
    }
    std::sort(response.references.begin(), response.references.end());
    if (std::adjacent_find(response.references.begin(), response.references.end()) != response.references.end()) {
        throw ProverError("COUNT: referencias repetidas en el resultado del AIT");
    }
    response.count = response.references.size();
    const std::unordered_set<std::int64_t> common(response.references.begin(), response.references.end());

    std::array<std::vector<aso::FieldSet>, 2> complements;
    for (int k = 0; k < 2; ++k) {
        const auto nodes = covers(response.ranges[k]);
        std::size_t offset = 0;
        for (const auto& node : nodes) {
            const auto n = static_cast<std::size_t>(node.header.count);
            if (offset > ranged[k].values.size() || n > ranged[k].values.size() - offset)
                throw ProverError("COUNT: contador AIT inconsistente");
            std::vector<std::int64_t> included, omitted;
            for (std::size_t i = 0; i < n; ++i) {
                const auto id = ranged[k].values[offset + i];
                (common.count(id) ? included : omitted).push_back(id);
            }
            offset += n;
            response.node_references[k].push_back(std::move(included));
            complements[k].push_back(elements(omitted));
            response.splits[k].push_back(aso::prove_split(complements[k].back(), pk));
        }
        if (offset != ranged[k].values.size()) throw ProverError("COUNT: cobertura incompleta");
    }
    for (const auto& a : complements[0]) {
        for (const auto& b : complements[1]) {
            response.disjoint.push_back(aso::prove_disjoint(a, b, pk));
        }
    }
    return answer;
}

CountVerification verify_count(const scheme::DatabaseDigest& digest,
                               const aso::PublicKey& pk, const CountQuery& asked,
                               const CountResponse& response) {
    CountVerification out;
    try {
        check_shape(asked);
        if (response.state_id != digest.id()) throw Rejected("digest distinto");
        if (!(response.query == asked)) throw Rejected("respuesta para otra consulta");
        if (response.count != response.references.size()) throw Rejected("COUNT distinto de los IDs publicados");
        if (!std::is_sorted(response.references.begin(), response.references.end()) ||
            std::adjacent_find(response.references.begin(), response.references.end()) != response.references.end()) {
            throw Rejected("IDs desordenados o repetidos");
        }
        for (const auto id : response.references) {
            if (id <= 0) throw Rejected("ID invalido en COUNT");
        }
        const std::array<RangeQuery, 2> predicates{asked.first, asked.second};
        std::array<ait::RangeVerification, 2> range;
        for (int k = 0; k < 2; ++k) {
            const auto spec = selection_tree(predicates[k], digest);
            const auto* tree = digest.find_tree(spec);
            if (!tree) throw Rejected("AIT no autenticado");
            range[k] = ait::verify_range(tree->root, predicates[k].lo, predicates[k].hi,
                                         response.ranges[k]);
            if (!range[k].accepted) throw Rejected("AIT " + spec.label() + ": " + range[k].reason);
            out.cover_nodes[k] = range[k].cover.size();
            if (response.count > range[k].count) throw Rejected("COUNT supera el contador del AIT");
            if (response.splits[k].size() != range[k].cover.size() ||
                response.node_references[k].size() != range[k].cover.size()) {
                throw Rejected("numero de testigos/particiones distinto del RangeCover");
            }
            if (!same_unique_references(response.node_references[k], response.references)) {
                throw Rejected("las referencias de los nodos no coinciden con COUNT");
            }
        }
        if (response.disjoint.size() != range[0].cover.size() * range[1].cover.size()) {
            throw Rejected("cantidad incorrecta de testigos de coprimalidad");
        }
        for (int k = 0; k < 2; ++k) {
            for (std::size_t i = 0; i < range[k].cover.size(); ++i) {
                const auto& published = response.node_references[k][i];
                if (published.size() > range[k].cover[i].header.count)
                    throw Rejected("nodo con mas IDs publicados que autenticados");
                if (!aso::verify_split(range[k].cover[i].payload.acc_g1,
                                       elements(published), response.splits[k][i], pk)) {
                    throw Rejected("ASO: division del nodo invalida");
                }
            }
        }
        std::size_t i = 0;
        for (const auto& a : response.splits[0]) {
            for (const auto& b : response.splits[1]) {
                if (!aso::verify_disjoint(a, b, response.disjoint[i++])) {
                    throw Rejected("ASO: interseccion incompleta (Bezout)");
                }
            }
        }
        out.accepted = true;
        out.count = response.count;
    } catch (const std::exception& e) {
        out.accepted = false;
        out.reason = e.what();
        out.count = 0;
    }
    return out;
}
}  // namespace tt::query

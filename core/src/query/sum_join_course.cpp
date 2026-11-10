#include "tt/query/sum_join_course.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "tt/ait/node.hpp"
#include "tt/aso/accumulator.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/db/row_hash.hpp"
#include "tt/merkle/tree.hpp"
#include "stage_metrics.hpp"

namespace tt::query {
namespace {
static const scheme::TreeSpec kCourseTree{"course", "course_id", "course_id"};
constexpr std::int64_t kLo = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kHi = std::numeric_limits<std::int64_t>::max();

/*
 * Convierte claves course_id distintas a elementos del campo usados por ASO.
 *
 * Parametros:
 *   keys  claves de la interseccion o de su complemento, sin repeticiones
 *
 * Devuelve:
 *   FieldSet con un elemento por clave; el orden no es relevante para ASO.
 */
aso::FieldSet keys_to_field(const std::unordered_set<std::int64_t>& keys) {
    aso::FieldSet result;
    result.elements.reserve(keys.size());
    for (const auto key : keys) result.elements.push_back(ait::value_to_element(key));
    return result;
}

/*
 * Recupera el acumulador de la raiz desde un RangeCover total ya validado.
 *
 * Parametros:
 *   range  verificacion AIT del intervalo completo de course
 *
 * Devuelve:
 *   Acumuladores G1/G2 autenticados; identidad del grupo para tabla vacia.
 *
 * Errores:
 *   Lanza runtime_error si una tabla no vacia carece de cobertura de raiz.
 */
aso::SplitWitness full_course_accumulator(const ait::RangeVerification& range) {
    const auto& group = crypto::init_pairing_group();
    if (range.count == 0 && range.cover.empty()) return {group.g1, group.g2};
    if (range.cover.size() != 1 || range.cover[0].header.count != range.count)
        throw std::runtime_error("C14: el rango total no cubre la raiz de course");
    return {range.cover[0].payload.acc_g1, range.cover[0].payload.acc_g2};
}

/*
 * Suma creditos en decimas con aritmetica entera exacta del Verifier.
 *
 * Parametros:
 *   total   acumulado que se actualiza tras comprobar el rango
 *   amount  creditos de una materia autenticada por Merkle
 *
 * Errores:
 *   Lanza overflow_error si el resultado excede int64.
 */
void checked_add(std::int64_t& total, std::int64_t amount) {
    const auto mx = std::numeric_limits<std::int64_t>::max();
    const auto mn = std::numeric_limits<std::int64_t>::min();
    if ((amount > 0 && total > mx - amount) ||
        (amount < 0 && total < mn - amount))
        throw std::overflow_error("C14: SUM fuera del rango int64");
    total += amount;
}
}  // namespace

MultiRangeQuery c14_filter(std::int64_t student_id) {
    return {{"academic_record", "student_id", student_id, student_id},
            {"academic_record", "grade", 6, kHi}};
}

SumJoinCourseAnswer answer_sum_join_course(const scheme::AuthenticatedDatabase& adb,
                                           const scheme::DatabaseDigest& digest,
                                           const aso::PublicKey& pk,
                                           std::int64_t student_id, db::RowStore& store) {
    SumJoinCourseAnswer out;
    auto& response = out.response;
    response.state_id = digest.id();
    response.student_id = student_id;
    auto filtered = answer_multirange(adb, digest, pk, c14_filter(student_id), store);
    response.filtered = std::move(filtered.response);
    out.stale_rows = filtered.stale_rows;

    const auto* course = adb.find_table("course");
    const auto* tree = adb.find_tree(kCourseTree);
    if (!digest.find_tree(kCourseTree) || !tree || !course)
        throw UnsupportedQuery("C14: falta AIT course(course_id -> course_id)");
    const auto course_key_col = course->schema.column_index("course_id");
    if (!course_key_col || *course_key_col != course->schema.reference_column)
        throw UnsupportedQuery("C14: course_id debe ser la referencia primaria de course");
    const auto* records = adb.find_table("academic_record");
    if (!records) throw ProverError("C14: falta academic_record en D~");
    const auto record_key_col = records->schema.column_index("course_id");
    if (!record_key_col) throw ProverError("C14: falta course_id en academic_record");

    std::unordered_set<std::int64_t> wanted;
    for (const auto& record : response.filtered.rows)
        wanted.insert(std::get<std::int64_t>(record.row.at(*record_key_col)));
    const auto all = metrics::measure(metrics::Stage::kAit, [&] { return tree->range(kLo, kHi); });
    response.course_range = all.proof;
    std::vector<std::int64_t> matching;
    std::unordered_set<std::int64_t> unmatched;
    for (const auto key : all.values) {
        if (wanted.count(key)) matching.push_back(key);
        else unmatched.insert(key);
    }
    // El AIT (course_id,course_id) autentica los course_id; se usan como
    // referencias para recuperar SOLO las materias participantes, no todas.
    const auto rows = store.fetch_rows(course->schema, matching);
    if (rows.size() != matching.size()) throw ProverError("C14: faltan filas de course");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto key = std::get<std::int64_t>(rows[i].at(*course_key_col));
        const auto pos = course->position_of(key);
        if (!pos) throw ProverError("C14: course no autenticado en D~");
        metrics::measure(metrics::Stage::kMerkle, [&] {
            if (db::row_hash(course->schema, rows[i]) != course->rows.leaves().at(*pos)) ++out.stale_rows;
            response.course_rows.push_back({rows[i], course->rows.prove(*pos)});
        });
    }
    // Split demuestra que cursos_publicados + resto = AIT_course.
    // Bezout(resto_course, claves_filtradas_distintas) impide ocultar un JOIN.
    metrics::measure(metrics::Stage::kAso, [&] {
        response.course_complement = aso::prove_split(keys_to_field(unmatched), pk);
        response.no_missing_courses = aso::prove_disjoint(keys_to_field(unmatched), keys_to_field(wanted), pk);
    });
    return out;
}

SumJoinCourseVerification verify_sum_join_course(const scheme::DatabaseDigest& digest,
                                                const aso::PublicKey& pk,
                                                std::int64_t student_id,
                                                const SumJoinCourseResponse& response) {
    SumJoinCourseVerification out;
    try {
        if (response.state_id != digest.id() || response.student_id != student_id)
            throw std::runtime_error("C14: digest o student_id incorrecto");
        const auto check_filter = verify_multirange(digest, pk, c14_filter(student_id), response.filtered);
        if (!check_filter.accepted)
            throw std::runtime_error("C14: C7 no verificado: " + check_filter.reason);
        out.filtered_rows = check_filter.rows.size();
        const auto* record = digest.find_table("academic_record");
        const auto* course = digest.find_table("course");
        const auto* tree = digest.find_tree(kCourseTree);
        if (!record || !course || !tree)
            throw std::runtime_error("C14: falta tabla o AIT publicado");
        const auto record_key_col = record->schema.column_index("course_id");
        const auto course_key_col = course->schema.column_index("course_id");
        const auto credit_col = course->schema.column_index("credits_tenths");
        if (!record_key_col || !course_key_col || !credit_col ||
            *course_key_col != course->schema.reference_column ||
            course->schema.columns[*credit_col].type != db::ColumnType::kInteger)
            throw std::runtime_error("C14: esquema de JOIN o SUM incorrecto");

        std::unordered_set<std::int64_t> requested;
        for (const auto& row : check_filter.rows)
            requested.insert(std::get<std::int64_t>(row.at(*record_key_col)));
        const auto range = metrics::measure(metrics::Stage::kAit, [&] {
            return ait::verify_range(tree->root, kLo, kHi, response.course_range);
        });
        if (!range.accepted) throw std::runtime_error("C14: AIT course invalido: " + range.reason);
        if (range.count != course->row_count)
            throw std::runtime_error("C14: cantidad de course distinta del AIT");
        const auto whole = full_course_accumulator(range);

        std::unordered_set<std::int64_t> seen_refs;
        std::unordered_map<std::int64_t, std::int64_t> course_credits;
        for (const auto& proof : response.course_rows) {
            const auto& row = proof.row;
            if (row.size() != course->schema.columns.size())
                throw std::runtime_error("C14: fila course de longitud incorrecta");
            for (std::size_t i = 0; i < row.size(); ++i) {
                if ((course->schema.columns[i].type == db::ColumnType::kInteger) !=
                    std::holds_alternative<std::int64_t>(row[i]))
                    throw std::runtime_error("C14: tipo incorrecto de celda course");
            }
            const auto key = std::get<std::int64_t>(row.at(*course_key_col));
            if (!seen_refs.insert(key).second || !requested.count(key))
                throw std::runtime_error("C14: materia duplicada o no solicitada");
            const bool inclusion_ok = metrics::measure(metrics::Stage::kMerkle, [&] {
                return merkle::verify_inclusion(course->rows_root, course->row_count,
                                                db::row_hash(course->schema, row), proof.inclusion);
            });
            if (!inclusion_ok)
                throw std::runtime_error("C14: Merkle course invalido");
            course_credits.emplace(key, std::get<std::int64_t>(row.at(*credit_col)));
        }
        if (course_credits.size() > range.count)
            throw std::runtime_error("C14: mas materias que course autenticado");
        const bool split_ok = metrics::measure(metrics::Stage::kAso, [&] {
            return aso::verify_split(whole.rest_g1, keys_to_field(seen_refs), response.course_complement, pk);
        });
        if (!split_ok)
            throw std::runtime_error("C14: Split ASO course invalido");
        // El conjunto completo de claves filtradas es conocido y autenticado
        // gracias a C7. Sus multiplicidades no afectan a la coprimalidad.
        const bool disjoint_ok = metrics::measure(metrics::Stage::kAso, [&] {
            const auto& group = crypto::init_pairing_group();
            const aso::SplitWitness left{aso::accumulate_g1(keys_to_field(requested), pk), group.g2};
            return aso::verify_disjoint(response.course_complement, left, response.no_missing_courses);
        });
        if (!disjoint_ok)
            throw std::runtime_error("C14: Bezout invalido; faltan materias coincidentes");

        std::int64_t sum = 0;
        for (const auto& row : check_filter.rows) {
            const auto key = std::get<std::int64_t>(row.at(*record_key_col));
            const auto it = course_credits.find(key);
            if (it == course_credits.end()) continue;  // INNER JOIN: clave ausente
            checked_add(sum, it->second);               // bag semantics: sin DISTINCT
            ++out.joined_rows;
        }
        out.course_rows = course_credits.size();
        if (out.joined_rows) out.credits_tenths = sum; // SQL SUM() vacia = NULL
        out.accepted = true;
    } catch (const std::exception& e) {
        out = SumJoinCourseVerification{};
        out.reason = e.what();
    }
    return out;
}
}  // namespace tt::query

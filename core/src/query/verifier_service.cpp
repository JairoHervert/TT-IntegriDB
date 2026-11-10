// verifier_service.cpp

#include "tt/query/verifier_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "tt/query/average.hpp"
#include "tt/query/count_intersection.hpp"
#include "tt/query/join_course.hpp"
#include "tt/query/join_program.hpp"
#include "tt/query/minimum.hpp"
#include "tt/query/multirange.hpp"
#include "tt/query/response_io.hpp"
#include "tt/query/selection.hpp"
#include "tt/query/sum_join_course.hpp"
#include "stage_metrics.hpp"

namespace tt::query {

namespace {

using Clock = std::chrono::steady_clock;

/*
 * Segundos transcurridos desde un instante del reloj monotono.
 */
double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/*
 * Estructura publicada en delta de una tabla.
 *
 * Errores:
 *   Lanza std::runtime_error si la tabla no esta en delta (no deberia pasar
 *   despues de required_trees).
 */
const db::TableSchema& schema_of(const scheme::DatabaseDigest& digest, const std::string& table) {
    const scheme::TableDigest* found = digest.find_table(table);
    if (found == nullptr) throw std::runtime_error("la tabla " + table + " no esta en delta");
    return found->schema;
}

/*
 * Convierte una celda de una fila de la base en valor del resultado.
 */
ResultValue to_value(const db::Cell& cell) {
    if (const auto* v = std::get_if<std::int64_t>(&cell)) return *v;
    return std::get<std::string>(cell);
}

/*
 * Arma una tabla con las columnas de una tabla de la base y sus filas.
 */
ResultTable table_of(const db::TableSchema& schema, const std::vector<db::Row>& rows) {
    ResultTable t;
    for (const db::Column& c : schema.columns) t.columns.push_back(c.name);
    t.rows.reserve(rows.size());
    for (const db::Row& row : rows) {
        std::vector<ResultValue> out;
        out.reserve(row.size());
        for (const db::Cell& cell : row) out.push_back(to_value(cell));
        t.rows.push_back(std::move(out));
    }
    return t;
}

/*
 * Arma el resultado de un JOIN muchos-a-uno como lo devolveria SQL: cada
 * fila de la izquierda seguida de la fila de la derecha con la misma clave.
 * Las columnas se nombran "tabla.columna". Las filas ya estan verificadas,
 * por lo que la clave es un entero.
 *
 * Parametros:
 *   left, right          estructura de cada tabla
 *   left_rows, right_rows filas verificadas de cada tabla
 *   key                  columna de union (existe en las dos tablas)
 */
ResultTable joined_table(const db::TableSchema& left, const db::TableSchema& right,
                         const std::vector<db::Row>& left_rows, const std::vector<db::Row>& right_rows,
                         const std::string& key) {
    ResultTable t;
    for (const db::Column& c : left.columns) t.columns.push_back(left.name + "." + c.name);
    for (const db::Column& c : right.columns) t.columns.push_back(right.name + "." + c.name);
    const std::size_t left_key = *left.column_index(key);
    const std::size_t right_key = *right.column_index(key);
    t.rows.reserve(left_rows.size());
    for (const db::Row& l : left_rows) {
        const std::int64_t k = std::get<std::int64_t>(l.at(left_key));
        for (const db::Row& r : right_rows) {
            if (std::get<std::int64_t>(r.at(right_key)) != k) continue;
            std::vector<ResultValue> row;
            row.reserve(l.size() + r.size());
            for (const db::Cell& cell : l) row.push_back(to_value(cell));
            for (const db::Cell& cell : r) row.push_back(to_value(cell));
            t.rows.push_back(std::move(row));
            break;
        }
    }
    return t;
}

/*
 * Tabla de una sola columna y una sola fila (AVG, COUNT, MIN, SUM).
 */
ResultTable scalar_table(const std::string& column, ResultValue value) {
    ResultTable t;
    t.columns.push_back(column);
    t.rows.push_back({std::move(value)});
    return t;
}

/*
 * Marca la respuesta como rechazada con su motivo.
 */
void reject(VerifierOutcome& out, const std::string& reason) {
    out.accepted = false;
    out.reason = reason;
    out.detail.clear();
    out.result = ResultTable{};
}

/*
 * Aplica el Verify que corresponde a la consulta y llena out. Puede lanzar
 * ResponseFormatError si los bytes no tienen el formato esperado.
 */
void verify_by_kind(const scheme::DatabaseDigest& digest, const aso::PublicKey& pk, const ParsedQuery& query,
                    const std::vector<unsigned char>& bytes, const VerifyOptions& options, VerifierOutcome& out) {
    switch (query_kind(query)) {
        case QueryKind::kSelection: {
            const SelectionResponse response = decode_response(bytes);
            const Check check = options.check_accumulators ? Check::kWithAccumulators : Check::kCounts;
            const SelectionVerification v = verify_selection(digest, pk, query.query, response, check);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            out.detail = std::to_string(v.rows.size()) + " filas (" + std::to_string(v.cover_nodes) +
                         " nodos de cobertura)";
            out.stages = {{"prueba del AIT", v.times.range}, {"filas (hash, Merkle y contadores)", v.times.rows}};
            if (options.check_accumulators) out.stages.push_back({"acumuladores (ASO)", v.times.accumulators});
            out.result = table_of(schema_of(digest, query.query.table), v.rows);
            return;
        }
        case QueryKind::kMultiRange: {
            const MultiRangeResponse response = decode_multirange_response(bytes);
            const MultiRangeVerification v = verify_multirange(digest, pk, *query.multi, response);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            out.detail = std::to_string(v.rows.size()) + " filas";
            out.stages = {{"pruebas de los AIT", v.range_seconds},
                          {"filas (hash y Merkle)", v.rows_seconds},
                          {"interseccion (ASO)", v.aso_seconds}};
            out.result = table_of(schema_of(digest, query.multi->first.table), v.rows);
            return;
        }
        case QueryKind::kJoinCourse: {
            const JoinCourseResponse response = decode_join_course_response(bytes);
            const JoinCourseVerification v = verify_join_course(digest, pk, response);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            out.detail = std::to_string(v.joined_rows) + " filas del JOIN; " + std::to_string(v.course_rows.size()) +
                         " unidades de aprendizaje";
            out.result = joined_table(schema_of(digest, "academic_record"), schema_of(digest, "course"),
                                      v.record_rows, v.course_rows, "course_id");
            return;
        }
        case QueryKind::kJoinProgram: {
            const JoinProgramResponse response = decode_join_program_response(bytes);
            const JoinProgramVerification v = verify_join_program(digest, pk, response);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            out.detail = std::to_string(v.joined_rows) + " filas del JOIN; " +
                         std::to_string(v.program_rows.size()) + " programas";
            out.result = joined_table(schema_of(digest, "student"), schema_of(digest, "program"), v.student_rows,
                                      v.program_rows, "program_id");
            return;
        }
        case QueryKind::kAverage: {
            const AverageResponse response = decode_average_response(bytes);
            const AverageVerification v = verify_average(digest, pk, *query.avg_student, response);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            if (v.count == 0) {
                out.detail = "AVG = NULL (el alumno no tiene registros)";
                out.result = scalar_table("AVG(grade)", std::monostate{});
            } else {
                const double average = static_cast<double>(v.sum) / static_cast<double>(v.count);
                out.detail = "SUM " + std::to_string(v.sum) + " / COUNT " + std::to_string(v.count) + " = AVG " +
                             value_text(average);
                out.result = scalar_table("AVG(grade)", average);
            }
            return;
        }
        case QueryKind::kCount: {
            const CountResponse response = decode_count_response(bytes);
            const CountVerification v = verify_count(digest, pk, *query.count, response);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            out.detail = "COUNT = " + std::to_string(v.count);
            out.result = scalar_table("COUNT(*)", static_cast<std::int64_t>(v.count));
            return;
        }
        case QueryKind::kMinimum: {
            const MultiRangeResponse response = decode_multirange_response(bytes);
            const MinimumVerification v = verify_minimum(digest, pk, *query.min_student, response);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            if (v.minimum) {
                out.detail = "MIN = " + std::to_string(*v.minimum) + " (" + std::to_string(v.witnesses_rows) +
                             " filas testigo)";
                out.result = scalar_table("MIN(grade)", *v.minimum);
            } else {
                out.detail = "MIN = NULL (el alumno no tiene registros)";
                out.result = scalar_table("MIN(grade)", std::monostate{});
            }
            return;
        }
        case QueryKind::kSumJoinCourse: {
            const SumJoinCourseResponse response = decode_sum_join_course_response(bytes);
            const SumJoinCourseVerification v =
                verify_sum_join_course(digest, pk, *query.sum_credits_student, response);
            if (!v.accepted) return reject(out, v.reason);
            out.accepted = true;
            if (v.credits_tenths) {
                char credits[32];
                std::snprintf(credits, sizeof credits, "%.1f", static_cast<double>(*v.credits_tenths) / 10.0);
                out.detail = "SUM = " + std::to_string(*v.credits_tenths) + " decimas de credito (" + credits +
                             " creditos); " + std::to_string(v.joined_rows) + " filas del JOIN; " +
                             std::to_string(v.course_rows) + " unidades de aprendizaje";
                out.result = scalar_table("SUM(course.credits_tenths)", *v.credits_tenths);
            } else {
                out.detail = "SUM = NULL (ninguna fila del JOIN)";
                out.result = scalar_table("SUM(course.credits_tenths)", std::monostate{});
            }
            return;
        }
    }
}

}  // namespace

/*
 * Implementacion: los mismos arboles que revisaban los comandos del
 * verificador antes de unificarse; selection_tree ya lanza UnsupportedQuery
 * si el AIT de una columna no existe.
 */
std::vector<scheme::TreeSpec> required_trees(const ParsedQuery& query, const scheme::DatabaseDigest& digest) {
    std::vector<scheme::TreeSpec> trees;
    auto require = [&](const scheme::TreeSpec& spec) {
        if (digest.find_tree(spec) == nullptr) {
            throw UnsupportedQuery("el estado autenticado no tiene el AIT " + spec.label() + " (plan '" +
                                   digest.plan + "'); la consulta no se puede verificar");
        }
        trees.push_back(spec);
    };
    switch (query_kind(query)) {
        case QueryKind::kSelection:
            trees.push_back(selection_tree(query.query, digest));
            break;
        case QueryKind::kMultiRange:
            trees.push_back(selection_tree(query.multi->first, digest));
            trees.push_back(selection_tree(query.multi->second, digest));
            break;
        case QueryKind::kCount:
            trees.push_back(selection_tree(query.count->first, digest));
            trees.push_back(selection_tree(query.count->second, digest));
            break;
        case QueryKind::kMinimum: {
            const std::int64_t s = *query.min_student;
            trees.push_back(selection_tree(RangeQuery{"academic_record", "student_id", s, s}, digest));
            trees.push_back(selection_tree(RangeQuery{"academic_record", "grade", 0, 0}, digest));
            break;
        }
        case QueryKind::kSumJoinCourse: {
            const MultiRangeQuery filter = c14_filter(*query.sum_credits_student);
            trees.push_back(selection_tree(filter.first, digest));
            trees.push_back(selection_tree(filter.second, digest));
            require({"course", "course_id", "course_id"});
            break;
        }
        case QueryKind::kJoinCourse:
            require({"academic_record", "course_id", "course_id"});
            require({"course", "course_id", "course_id"});
            break;
        case QueryKind::kJoinProgram:
            require({"student", "program_id", "program_id"});
            require({"program", "program_id", "program_id"});
            break;
        case QueryKind::kAverage:
            require({"academic_record", "student_id", "grade"});
            break;
    }
    return trees;
}

/*
 * Implementacion: cualquier problema con los bytes (formato invalido o una
 * excepcion al revisarlos) se convierte en rechazo; nunca se acepta una
 * respuesta que no paso todas las comprobaciones.
 */
VerifierOutcome verify_query(const scheme::DatabaseDigest& digest, const aso::PublicKey& public_key,
                             const ParsedQuery& query, const std::vector<unsigned char>& bytes,
                             const VerifyOptions& options) {
    VerifierOutcome out;
    const Clock::time_point start = Clock::now();
    const QueryKind kind = query_kind(query);
    const bool profile = kind == QueryKind::kJoinCourse || kind == QueryKind::kJoinProgram ||
                         kind == QueryKind::kSumJoinCourse;
    metrics::Totals measured;
    metrics::Capture capture(measured, profile);
    try {
        verify_by_kind(digest, public_key, query, bytes, options, out);
    } catch (const ResponseFormatError& e) {
        reject(out, std::string("la respuesta no tiene un formato valido: ") + e.what());
    } catch (const std::exception& e) {
        reject(out, std::string("la respuesta no se pudo revisar: ") + e.what());
    }
    out.seconds = seconds_since(start);
    if (profile) {
        // No se confunde la decodificacion, el JOIN local ni la conversion
        // del resultado con tiempo criptografico puro.
        const double accounted = measured.ait_seconds + measured.merkle_seconds + measured.aso_seconds;
        out.stages = {{"AIT (operaciones instrumentadas)", measured.ait_seconds},
                      {"Merkle (hash e inclusion)", measured.merkle_seconds},
                      {"ASO (Split y Bezout)", measured.aso_seconds},
                      {"Otros costos de Verify", std::max(0.0, out.seconds - accounted)}};
    }
    return out;
}

/*
 * Implementacion: un caso por alternativa del variant.
 */
std::string value_text(const ResultValue& value) {
    if (std::holds_alternative<std::monostate>(value)) return "NULL";
    if (const auto* i = std::get_if<std::int64_t>(&value)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&value)) {
        char text[64];
        std::snprintf(text, sizeof text, "%.6f", *d);
        return text;
    }
    return std::get<std::string>(value);
}

}  // namespace tt::query

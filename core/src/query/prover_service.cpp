// prover_service.cpp

#include "tt/query/prover_service.hpp"

#include <chrono>
#include <utility>
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

/*
 * Devuelve la clave publica del contexto o explica por que falta.
 *
 * Parametros:
 *   context  contexto del probador
 *   query    consulta que la necesita (para el mensaje)
 *
 * Errores:
 *   Lanza ProverError si el probador no cargo la clave publica.
 */
const aso::PublicKey& public_key_for(const ProverContext& context, const ParsedQuery& query) {
    if (context.public_key == nullptr) {
        throw ProverError("la consulta " + query.catalog_id +
                          " necesita la clave publica para generar los testigos del ASO y el probador no la cargo");
    }
    return *context.public_key;
}

// Mide el tiempo de lectura de filas sin cambiar el contrato RowStore ni
// introducir dependencias de MySQL en el nucleo criptografico.
class TimedRowStore final : public db::RowStore {
public:
    explicit TimedRowStore(db::RowStore* source) : source_(source) {}

    std::vector<db::Row> fetch_rows(const db::TableSchema& schema,
                                    const std::vector<std::int64_t>& references) override {
        if (source_ == nullptr) throw ProverError("el probador no tiene acceso a la base D para leer filas");
        const auto start = std::chrono::steady_clock::now();
        std::vector<db::Row> rows = source_->fetch_rows(schema, references);
        seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        ++calls;
        count += rows.size();
        return rows;
    }

    std::string description() const override { return source_ ? source_->description() : "origen no disponible"; }

    double seconds = 0;
    std::size_t calls = 0;
    std::size_t count = 0;

private:
    db::RowStore* source_ = nullptr;
};

}  // namespace

/*
 * Implementacion: solo las consultas de seleccion y rango en una columna se
 * responden sin testigos del ASO.
 */
bool needs_public_key(const ParsedQuery& query) { return query_kind(query) != QueryKind::kSelection; }

/*
 * Implementacion: C10 (promedio) y C11/C12 (conteo) responden con valores
 * y testigos tomados de D~, sin filas.
 */
bool reads_rows(const ParsedQuery& query) {
    const QueryKind kind = query_kind(query);
    return kind != QueryKind::kAverage && kind != QueryKind::kCount;
}

/*
 * Implementacion: un caso por tipo de consulta; cada uno llama a la funcion
 * ProofGen del nucleo, codifica la respuesta con su encode_* y arma un
 * resumen con los mismos datos que mostraban los comandos originales.
 */
ProverReply answer_query(const ProverContext& context, const ParsedQuery& query) {
    if (context.database == nullptr || context.digest == nullptr) {
        throw ProverError("el probador no tiene cargado el estado autenticado");
    }
    const scheme::AuthenticatedDatabase& adb = *context.database;
    const scheme::DatabaseDigest& digest = *context.digest;
    ProverReply reply;
    TimedRowStore rows(context.store);
    const QueryKind kind = query_kind(query);
    const bool profile = kind == QueryKind::kJoinCourse || kind == QueryKind::kJoinProgram ||
                         kind == QueryKind::kSumJoinCourse;
    metrics::Totals measured;
    metrics::Capture capture(measured, profile);
    using Clock = std::chrono::steady_clock;
    const auto generation_start = Clock::now();
    // Cierra la medicion de ProofGen e inicia la de codificacion. Cada
    // variante conserva el mismo formato TTRSP que devolvia anteriormente.
    auto encode = [&](auto&& encoder) {
        reply.generation_seconds = std::chrono::duration<double>(Clock::now() - generation_start).count();
        const auto encoding_start = Clock::now();
        reply.bytes = encoder();
        reply.encoding_seconds = std::chrono::duration<double>(Clock::now() - encoding_start).count();
    };

    switch (query_kind(query)) {
        case QueryKind::kSelection: {
            const SelectionAnswer answer = answer_selection(adb, digest, query.query, rows);
            encode([&] { return encode_response(answer.response); });
            reply.stale_rows = answer.stale_rows;
            reply.summary = "AIT " + answer.tree.label() + ": " + std::to_string(answer.response.rows.size()) +
                            " filas; prueba de " + std::to_string(answer.response.range.entries.size()) + " nodos";
            break;
        }
        case QueryKind::kMultiRange: {
            const MultiRangeAnswer answer =
                answer_multirange(adb, digest, public_key_for(context, query), *query.multi, rows);
            encode([&] { return encode_multirange_response(answer.response); });
            reply.stale_rows = answer.stale_rows;
            reply.summary = "AIT " + answer.trees[0].label() + " con " + std::to_string(answer.input_counts[0]) +
                            " referencias y AIT " + answer.trees[1].label() + " con " +
                            std::to_string(answer.input_counts[1]) + "; interseccion de " +
                            std::to_string(answer.response.rows.size()) + " filas";
            break;
        }
        case QueryKind::kJoinCourse: {
            const JoinCourseAnswer answer =
                answer_join_course(adb, digest, public_key_for(context, query), rows);
            encode([&] { return encode_join_course_response(answer.response); });
            reply.stale_rows = answer.stale_rows;
            reply.summary = std::to_string(answer.response.record_rows.size()) + " registros y " +
                            std::to_string(answer.response.course_rows.size()) + " unidades de aprendizaje";
            break;
        }
        case QueryKind::kJoinProgram: {
            const JoinProgramAnswer answer =
                answer_join_program(adb, digest, public_key_for(context, query), rows);
            encode([&] { return encode_join_program_response(answer.response); });
            reply.stale_rows = answer.stale_rows;
            reply.summary = std::to_string(answer.response.student_rows.size()) + " alumnos y " +
                            std::to_string(answer.response.program_rows.size()) + " programas";
            break;
        }
        case QueryKind::kAverage: {
            const AverageResponse response =
                answer_average(adb, digest, public_key_for(context, query), *query.avg_student);
            encode([&] { return encode_average_response(response); });
            reply.summary = response.count == 0 ? std::string("alumno sin registros (AVG = NULL)")
                                                : "SUM " + std::to_string(response.sum) + " y COUNT " +
                                                      std::to_string(response.count) + "; sin leer filas de D";
            break;
        }
        case QueryKind::kCount: {
            const CountAnswer answer = answer_count(adb, digest, public_key_for(context, query), *query.count);
            encode([&] { return encode_count_response(answer.response); });
            reply.summary = "COUNT " + std::to_string(answer.response.count) + " a partir de " +
                            std::to_string(answer.input_counts[0]) + " y " + std::to_string(answer.input_counts[1]) +
                            " referencias; sin leer filas de D";
            break;
        }
        case QueryKind::kMinimum: {
            const MinimumAnswer answer =
                answer_minimum(adb, digest, public_key_for(context, query), *query.min_student, rows);
            encode([&] { return encode_multirange_response(answer.range_answer.response); });
            reply.stale_rows = answer.range_answer.stale_rows;
            reply.summary = answer.minimum ? "MIN " + std::to_string(*answer.minimum) + " con " +
                                                 std::to_string(answer.range_answer.response.rows.size()) +
                                                 " filas testigo"
                                           : std::string("alumno sin registros (MIN = NULL)");
            break;
        }
        case QueryKind::kSumJoinCourse: {
            const SumJoinCourseAnswer answer = answer_sum_join_course(
                adb, digest, public_key_for(context, query), *query.sum_credits_student, rows);
            encode([&] { return encode_sum_join_course_response(answer.response); });
            reply.stale_rows = answer.stale_rows;
            reply.summary = std::to_string(answer.response.filtered.rows.size()) + " registros aprobados y " +
                            std::to_string(answer.response.course_rows.size()) + " unidades de aprendizaje";
            break;
        }
    }
    reply.detailed_stages = profile;
    if (reply.detailed_stages) {
        reply.ait_seconds = measured.ait_seconds;
        reply.merkle_seconds = measured.merkle_seconds;
        reply.aso_seconds = measured.aso_seconds;
    }
    reply.row_fetch_seconds = rows.seconds;
    reply.row_fetch_calls = rows.calls;
    reply.row_fetch_rows = rows.count;
    return reply;
}

}  // namespace tt::query

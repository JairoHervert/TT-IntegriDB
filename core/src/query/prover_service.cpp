// prover_service.cpp

#include "tt/query/prover_service.hpp"

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

/*
 * Devuelve el origen de filas del contexto o explica por que falta.
 *
 * Errores:
 *   Lanza ProverError si el probador no tiene acceso a la base D.
 */
db::RowStore& store_for(const ProverContext& context) {
    if (context.store == nullptr) throw ProverError("el probador no tiene acceso a la base D para leer filas");
    return *context.store;
}

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

    switch (query_kind(query)) {
        case QueryKind::kSelection: {
            const SelectionAnswer answer = answer_selection(adb, digest, query.query, store_for(context));
            reply.bytes = encode_response(answer.response);
            reply.stale_rows = answer.stale_rows;
            reply.summary = "AIT " + answer.tree.label() + ": " + std::to_string(answer.response.rows.size()) +
                            " filas; prueba de " + std::to_string(answer.response.range.entries.size()) + " nodos";
            break;
        }
        case QueryKind::kMultiRange: {
            const MultiRangeAnswer answer =
                answer_multirange(adb, digest, public_key_for(context, query), *query.multi, store_for(context));
            reply.bytes = encode_multirange_response(answer.response);
            reply.stale_rows = answer.stale_rows;
            reply.summary = "AIT " + answer.trees[0].label() + " con " + std::to_string(answer.input_counts[0]) +
                            " referencias y AIT " + answer.trees[1].label() + " con " +
                            std::to_string(answer.input_counts[1]) + "; interseccion de " +
                            std::to_string(answer.response.rows.size()) + " filas";
            break;
        }
        case QueryKind::kJoinCourse: {
            const JoinCourseAnswer answer =
                answer_join_course(adb, digest, public_key_for(context, query), store_for(context));
            reply.bytes = encode_join_course_response(answer.response);
            reply.stale_rows = answer.stale_rows;
            reply.summary = std::to_string(answer.response.record_rows.size()) + " registros y " +
                            std::to_string(answer.response.course_rows.size()) + " unidades de aprendizaje";
            break;
        }
        case QueryKind::kJoinProgram: {
            const JoinProgramAnswer answer =
                answer_join_program(adb, digest, public_key_for(context, query), store_for(context));
            reply.bytes = encode_join_program_response(answer.response);
            reply.stale_rows = answer.stale_rows;
            reply.summary = std::to_string(answer.response.student_rows.size()) + " alumnos y " +
                            std::to_string(answer.response.program_rows.size()) + " programas";
            break;
        }
        case QueryKind::kAverage: {
            const AverageResponse response =
                answer_average(adb, digest, public_key_for(context, query), *query.avg_student);
            reply.bytes = encode_average_response(response);
            reply.summary = response.count == 0 ? std::string("alumno sin registros (AVG = NULL)")
                                                : "SUM " + std::to_string(response.sum) + " y COUNT " +
                                                      std::to_string(response.count) + "; sin leer filas de D";
            break;
        }
        case QueryKind::kCount: {
            const CountAnswer answer = answer_count(adb, digest, public_key_for(context, query), *query.count);
            reply.bytes = encode_count_response(answer.response);
            reply.summary = "COUNT " + std::to_string(answer.response.count) + " a partir de " +
                            std::to_string(answer.input_counts[0]) + " y " + std::to_string(answer.input_counts[1]) +
                            " referencias; sin leer filas de D";
            break;
        }
        case QueryKind::kMinimum: {
            const MinimumAnswer answer =
                answer_minimum(adb, digest, public_key_for(context, query), *query.min_student, store_for(context));
            reply.bytes = encode_multirange_response(answer.range_answer.response);
            reply.stale_rows = answer.range_answer.stale_rows;
            reply.summary = answer.minimum ? "MIN " + std::to_string(*answer.minimum) + " con " +
                                                 std::to_string(answer.range_answer.response.rows.size()) +
                                                 " filas testigo"
                                           : std::string("alumno sin registros (MIN = NULL)");
            break;
        }
        case QueryKind::kSumJoinCourse: {
            const SumJoinCourseAnswer answer = answer_sum_join_course(
                adb, digest, public_key_for(context, query), *query.sum_credits_student, store_for(context));
            reply.bytes = encode_sum_join_course_response(answer.response);
            reply.stale_rows = answer.stale_rows;
            reply.summary = std::to_string(answer.response.filtered.rows.size()) + " registros aprobados y " +
                            std::to_string(answer.response.course_rows.size()) + " unidades de aprendizaje";
            break;
        }
    }
    return reply;
}

}  // namespace tt::query

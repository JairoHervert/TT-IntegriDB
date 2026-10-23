#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "tt/query/multirange.hpp"

namespace tt::query {
/*
 * C13 adapta MIN a un rango verificable (IntegriDB, seccion 4.4):
 * student_id = alumno AND grade <= minimo propuesto.
 * El servidor obtiene el candidato desde el AIT student_id -> grade, ya
 * construido por C10. NO es una prueba en si misma: el verificador valida
 * el rango con C7 (AIT + ASO + Merkle) y exige que todas sus filas tengan
 * exactamente el valor propuesto.
 * Si el alumno no tiene registros, RangeCover prueba que el conjunto es
 * vacio y se devuelve SQL NULL, sin usar valores sentinela en la interfaz.
 * Reutilizamos el formato TTRSP tipo 2 de MultiRangeResponse.
 */
/*
 * Resultado del probador de MIN: respuesta de rango multidimensional y
 * candidato opcional; std::nullopt representa NULL en SQL.
 */
struct MinimumAnswer {
    MultiRangeAnswer range_answer;
    std::optional<std::int64_t> minimum;
};

/*
 * Veredicto de MIN: el minimo solo se utiliza si accepted es true;
 * witnesses_rows indica cuantas filas autenticadas sustentan el minimo.
 */
struct MinimumVerification {
    bool accepted = false;
    std::string reason;
    std::optional<std::int64_t> minimum;  // NULL SQL si no hay registros
    std::size_t witnesses_rows = 0;
};

/*
 * Genera C13 con el AIT student_id -> grade para obtener un candidato y
 * el RangeCover multidimensional de C7 para autenticar su minimalidad.
 * Recupera las filas testigo de MySQL o del origen de prueba.
 *
 * Parametros:
 *   adb         estado autenticado que contiene los AIT
 *   digest      digest que compromete el estado
 *   pk          clave publica ASO para generar los testigos
 *   student_id  alumno cuya calificacion minima se solicita
 *   store       origen de filas reales (MySQL o memoria)
 *
 * Devuelve:
 *   Respuesta verificable de rango y valor minimo opcional.
 *
 * Errores:
 *   Lanza UnsupportedQuery si falta un AIT requerido, ProverError si el
 *   estado es inconsistente y propaga los errores del origen de filas.
 */
MinimumAnswer answer_minimum(const scheme::AuthenticatedDatabase& adb,
                             const scheme::DatabaseDigest& digest,
                             const aso::PublicKey& pk, std::int64_t student_id,
                             db::RowStore& store);

/*
 * Verifica la minimalidad declarada por C13 mediante la prueba AIT/ASO y
 * Merkle de C7; comprueba ademas que todas las filas tienen el candidato.
 * Un resultado vacio solo se acepta como NULL si el alumno no tiene filas.
 *
 * Parametros:
 *   digest      estado autenticado confiable
 *   pk          clave publica ASO
 *   student_id  alumno solicitado
 *   response    respuesta no confiable enviada por el probador
 *
 * Devuelve:
 *   accepted, motivo de rechazo, minimo opcional y filas testigo.
 *   Los errores de validacion se convierten en accepted=false.
 */
MinimumVerification verify_minimum(const scheme::DatabaseDigest& digest,
                                   const aso::PublicKey& pk, std::int64_t student_id,
                                   const MultiRangeResponse& response);
}  // namespace tt::query

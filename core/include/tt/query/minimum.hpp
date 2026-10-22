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
struct MinimumAnswer {
    MultiRangeAnswer range_answer;
    std::optional<std::int64_t> minimum;
};

struct MinimumVerification {
    bool accepted = false;
    std::string reason;
    std::optional<std::int64_t> minimum;  // NULL SQL si no hay registros
    std::size_t witnesses_rows = 0;
};

MinimumAnswer answer_minimum(const scheme::AuthenticatedDatabase& adb,
                             const scheme::DatabaseDigest& digest,
                             const aso::PublicKey& pk, std::int64_t student_id,
                             db::RowStore& store);

MinimumVerification verify_minimum(const scheme::DatabaseDigest& digest,
                                   const aso::PublicKey& pk, std::int64_t student_id,
                                   const MultiRangeResponse& response);
}  // namespace tt::query

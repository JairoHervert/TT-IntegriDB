#pragma once

#include <cstdint>
#include <string>

#include "tt/ait/range_proof.hpp"
#include "tt/aso/sum.hpp"
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"

namespace tt::query {
/*
 * Respuesta C10 para AVG(grade) de un alumno.
 * Transporta SUM y COUNT junto con el RangeCover y los testigos ASO.
 * Para COUNT=0 el resultado SQL es NULL; no se transmite una division.
 * La validez de SUM y COUNT se decide exclusivamente en el verificador.
 */
struct AverageResponse {
    crypto::Digest state_id{};
    std::int64_t student_id = 0;
    std::uint64_t count = 0;
    std::int64_t sum = 0;
    ait::RangeProof range;
    aso::SumWitness proof;
};
/*
 * Veredicto de C10. El promedio solo puede derivarse cuando accepted=true:
 * AVG = sum / count si count>0; AVG = NULL si count=0.
 */
struct AverageVerification {
    bool accepted = false;
    std::string reason;
    std::uint64_t count = 0;
    std::int64_t sum = 0;
};
/*
 * Genera la respuesta verificable de C10 a partir del AIT existente
 * academic_record(student_id -> grade), sin ejecutar SELECT en MySQL.
 * Utiliza el contador de la hoja y prueba criptograficamente su SUM.
 *
 * Parametros:
 *   adb         estado autenticado con los AIT del propietario
 *   digest      compromiso publicado del estado
 *   pk          clave publica ASO
 *   student_id  identificador del alumno consultado
 *
 * Devuelve:
 *   COUNT, SUM, RangeCover y testigos ASO, ligados al digest.
 *
 * Errores:
 *   Lanza UnsupportedQuery si falta el AIT requerido.
 *   Rechaza valores fuera del dominio y posibles desbordamientos.
 */
AverageResponse answer_average(const scheme::AuthenticatedDatabase& adb,
                               const scheme::DatabaseDigest& digest,
                               const aso::PublicKey& pk, std::int64_t student_id);

/*
 * Comprueba la respuesta de C10 sin acceso a MySQL ni a la clave privada.
 * Verifica el digest, el alumno, el RangeCover, COUNT y SUM por ASO.
 * No acepta el AVG declarado por el servidor: el cliente lo calcula.
 *
 * Parametros:
 *   digest      compromiso de estado confiable
 *   pk          clave publica ASO
 *   student_id  alumno solicitado por el cliente
 *   response    respuesta no confiable del probador
 *
 * Devuelve:
 *   Veredicto con accepted, reason, count y sum autenticados.
 *   Si count=0 y accepted=true, el AVG de SQL es NULL.
 */
AverageVerification verify_average(const scheme::DatabaseDigest& digest,
                                   const aso::PublicKey& pk, std::int64_t student_id,
                                   const AverageResponse& response);
}

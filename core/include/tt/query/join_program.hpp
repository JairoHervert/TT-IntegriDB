#pragma once

// C9: JOIN completo student.program_id = program.program_id.
// Los AIT (i,i) autentican los multiconjuntos de claves; dos separaciones
// ASO y dos certificados de coprimalidad evitan omitir coincidencias.
// Las filas de ambas tablas se autentican por Merkle, sin confiar en D~.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "tt/ait/range_proof.hpp"
#include "tt/aso/intersection.hpp"
#include "tt/db/row_store.hpp"
#include "tt/query/selection.hpp"
#include "tt/scheme/authenticated_db.hpp"

namespace tt::query {

struct JoinProgramResponse {
    crypto::Digest state_id{};
    std::array<ait::RangeProof, 2> ranges;          // student, program: rango total
    std::array<aso::SplitWitness, 2> splits;        // multiconjuntos no publicados
    std::array<aso::DisjointWitness, 2> disjoint;   // resto_student x program; resto_program x student
    std::vector<RowProof> student_rows;
    std::vector<RowProof> program_rows;
};

struct JoinProgramAnswer {
    JoinProgramResponse response;
    std::size_t stale_rows = 0;
};

struct JoinProgramVerification {
    bool accepted = false;
    std::string reason;
    std::vector<db::Row> student_rows;
    std::vector<db::Row> program_rows;
    std::size_t joined_rows = 0;
};

/*
 * Genera la respuesta de C9 a partir de ambos AIT y filas de MySQL.
 * Cada valor de program_id se conserva con su multiplicidad en student.
 *
 * Parametros:
 *   adb     base autenticada que recibe el Prover (nunca contiene sk)
 *   digest  compromiso del estado publicado durante Setup
 *   pk      clave publica ASO para construir los testigos bilineales
 *   store   origen de las filas completas, separado del nucleo criptografico
 *
 * Devuelve:
 *   Resultado de JOIN y certificados de completitud y autenticidad.
 *
 * Errores:
 *   Lanza UnsupportedQuery cuando falta un AIT, y ProverError cuando no
 *   coincide D~ con las filas disponibles; propaga errores de ASO y MySQL.
 */
JoinProgramAnswer answer_join_program(const scheme::AuthenticatedDatabase& adb,
                                      const scheme::DatabaseDigest& digest,
                                      const aso::PublicKey& pk, db::RowStore& store);

/*
 * Comprueba C9 usando exclusivamente pk, delta y la respuesta recibida.
 * Nunca consulta MySQL, D~, la clave secreta ni archivos del Owner.
 *
 * Parametros:
 *   digest    compromiso de estado autorizado para esta consulta
 *   pk        clave publica ASO correspondiente a digest
 *   response  filas y testigos no confiables enviados por el Prover
 *
 * Devuelve:
 *   accepted=true solo si no faltan ni sobran filas en el JOIN, y todas
 *   estan autenticadas por sus respectivos arboles de Merkle.
 *
 * Errores:
 *   Devuelve accepted=false y reason ante resultados o pruebas invalidas.
 *   No necesita conectar con MySQL ni conocer la clave secreta del Owner.
 */
JoinProgramVerification verify_join_program(const scheme::DatabaseDigest& digest,
                                            const aso::PublicKey& pk,
                                            const JoinProgramResponse& response);

}  // namespace tt::query

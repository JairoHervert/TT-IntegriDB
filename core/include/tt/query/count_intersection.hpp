#pragma once

// C11/C12: COUNT(*) autenticado mediante dos RangeCover y ASO.
// El resultado visible es un escalar; los record_id constituyen parte de la
// prueba (no se transfieren filas completas ni se lee MySQL en ProofGen).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "tt/aso/intersection.hpp"
#include "tt/query/query.hpp"
#include "tt/ait/range_proof.hpp"
#include "tt/scheme/authenticated_db.hpp"

namespace tt::query {

struct CountResponse {
    crypto::Digest state_id{};
    CountQuery query;
    std::uint64_t count = 0;
    std::vector<std::int64_t> references; // IDs unicos, ascendentes
    std::array<ait::RangeProof, 2> ranges;
    // Por cada AIT: valores de la interseccion asignados a cada nodo de cobertura.
    // La pertenencia de estos IDs queda demostrada por su respectivo SplitWitness.
    std::array<std::vector<std::vector<std::int64_t>>, 2> node_references;
    std::array<std::vector<aso::SplitWitness>, 2> splits;
    std::vector<aso::DisjointWitness> disjoint;
};

struct CountAnswer {
    CountResponse response;
    std::array<scheme::TreeSpec, 2> trees;
    std::array<std::uint64_t, 2> input_counts{};
};

struct CountVerification {
    bool accepted = false;
    std::string reason;
    std::uint64_t count = 0;
    std::array<std::size_t, 2> cover_nodes{};
};

/*
 * Construye la prueba de COUNT(*) para C11 o C12 a partir de dos AIT del estado
 * autenticado. No lee MySQL: solo usa los identificadores presentes en D~.
 *
 * Parametros:
 *   adb     base autenticada D~ vigente
 *   digest  digest delta que autentica D~
 *   pk      clave publica ASO
 *   query   consulta C11 (alumno y grade < 6) o C12 (dos igualdades)
 *
 * Devuelve:
 *   El escalar COUNT, las pruebas AIT y los testigos ASO de completitud.
 *
 * Errores:
 *   Lanza UnsupportedQuery si la forma no es C11/C12 o faltan arboles;
 *   ProverError si D~ no corresponde con los arboles esperados.
 */
CountAnswer answer_count(const scheme::AuthenticatedDatabase& adb,
                         const scheme::DatabaseDigest& digest,
                         const aso::PublicKey& pk, const CountQuery& query);

/*
 * Verifica el valor de COUNT sin consultar MySQL ni confiar en los IDs
 * publicados: cada ID debe ser miembro autenticado de ambos rangos y los
 * complementos deben ser disjuntos bajo la prueba de Bezout.
 *
 * Parametros:
 *   digest    estado autenticado vigente
 *   pk        clave publica ASO
 *   asked     consulta solicitada por el cliente
 *   response  respuesta y testigos del servidor
 *
 * Devuelve:
 *   accepted y count si la respuesta es correcta; motivo de rechazo en otro caso.
 */
CountVerification verify_count(const scheme::DatabaseDigest& digest,
                               const aso::PublicKey& pk, const CountQuery& asked,
                               const CountResponse& response);

}  // namespace tt::query

#pragma once

#include <array>
#include <string>
#include <vector>

#include "tt/aso/intersection.hpp"
#include "tt/query/selection.hpp"

namespace tt::query {
/*
 * Respuesta serializable de C7: estado y consulta, dos pruebas RangeCover,
 * testigos de separacion por nodo, certificados de coprimalidad por cada
 * pareja de nodos y filas de la interseccion con pruebas Merkle.
 */
struct MultiRangeResponse {
    crypto::Digest state_id{};
    MultiRangeQuery query;
    std::array<ait::RangeProof, 2> ranges;
    std::array<std::vector<aso::SplitWitness>, 2> splits; // un testigo por nodo de cobertura
    std::vector<aso::DisjointWitness> disjoint;            // orden i*|B|+j
    std::vector<RowProof> rows;                            // solo la interseccion
};
/*
 * Resultado del probador para C7. Incluye la respuesta verificable y datos
 * auxiliares para informar los AIT utilizados, cantidades y filas obsoletas.
 */
struct MultiRangeAnswer {
    MultiRangeResponse response;
    std::array<scheme::TreeSpec, 2> trees;
    std::array<std::uint64_t, 2> input_counts{};
    std::size_t stale_rows = 0;
};
/*
 * Resultado del verificador C7. Si accepted es false, rows queda vacio;
 * reason explica el rechazo. Los tiempos se reportan en segundos.
 */
struct MultiRangeVerification {
    bool accepted = false;
    std::string reason;
    std::vector<db::Row> rows;
    std::array<std::size_t, 2> cover_nodes{};
    double range_seconds = 0;
    double rows_seconds = 0;
    double aso_seconds = 0;
};

/*
 * ProofGen de C7: ejecuta RangeCover sobre los AIT de grade y term_code,
 * intersecta sus identificadores, recupera las filas de D y adjunta pruebas
 * Merkle y testigos ASO de separacion y coprimalidad.
 *
 * Parametros:
 *   adb     base autenticada D~ con los AIT y arboles Merkle de Setup
 *   digest  digest delta vigente que identifica los AIT admitidos
 *   pk      clave publica ASO (no se requiere la clave secreta)
 *   query   rangos de grade y term_code de la consulta C7
 *   store   origen de las filas reales de D (MySQL o memoria)
 *
 * Devuelve:
 *   Respuesta C7 y estadisticas auxiliares del probador.
 *
 * Errores:
 *   Lanza UnsupportedQuery si la forma de C7 o sus AIT no son validos,
 *   ProverError si faltan arboles o filas o hay contadores inconsistentes;
 *   propaga errores del origen de filas y de los testigos ASO.
 */
MultiRangeAnswer answer_multirange(const scheme::AuthenticatedDatabase& adb,
                                  const scheme::DatabaseDigest& digest,
                                  const aso::PublicKey& pk, const MultiRangeQuery& query,
                                  db::RowStore& store);
/*
 * Verify de C7: autentica los dos RangeCover contra delta, revisa cada fila
 * con Merkle y sus predicados, y valida los testigos ASO de separacion y
 * coprimalidad para descartar identificadores comunes omitidos.
 *
 * Parametros:
 *   digest    digest delta vigente del propietario
 *   pk        clave publica ASO, cuya huella debe verificarse contra delta
 *             antes de invocar esta funcion
 *   asked     consulta C7 solicitada por el cliente
 *   response  respuesta y pruebas recibidas del servidor
 *
 * Devuelve:
 *   accepted = true y las filas verificadas, o accepted = false, rows
 *   vacio y el motivo en reason; incluye tiempos por etapa.
 *
 * Errores:
 *   Captura los errores de validacion y los comunica como rechazo en vez
 *   de propagar excepciones por datos enviados por el servidor.
 */
MultiRangeVerification verify_multirange(const scheme::DatabaseDigest& digest,
                                         const aso::PublicKey& pk,
                                         const MultiRangeQuery& asked,
                                         const MultiRangeResponse& response);
}  // namespace tt::query

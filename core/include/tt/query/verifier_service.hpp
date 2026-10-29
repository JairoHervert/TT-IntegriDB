// verifier_service.hpp
//
// Punto de entrada unico del modulo verificador: recibe la consulta que hizo
// el cliente y los bytes que envio el probador, aplica el Verify que le
// corresponde y devuelve el resultado ya verificado como una tabla.
//
// Igual que prover_service, sirve para que la verificacion de un archivo
// (tt_verifier verify), la de una respuesta recibida por la red
// (tt_verifier query) y, mas adelante, la interfaz del caso de estudio usen
// exactamente las mismas reglas. Tampoco conoce la red: recibe bytes.
//
// Los bytes del probador nunca se consideran confiables. Si no tienen el
// formato esperado para la consulta, o si alguna comprobacion falla, el
// resultado es "rechazada" con su motivo y sin filas; el servicio no deja
// escapar excepciones por datos del servidor.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "tt/aso/keys.hpp"
#include "tt/query/query.hpp"
#include "tt/scheme/digest.hpp"

namespace tt::query {

// Un valor del resultado: NULL de SQL (std::monostate), entero, decimal o
// texto. Los decimales solo aparecen en AVG.
using ResultValue = std::variant<std::monostate, std::int64_t, double, std::string>;

// Resultado verificado, siempre como tabla: las consultas que devuelven un
// solo valor (AVG, COUNT, MIN, SUM) son una tabla de una columna y una fila.
struct ResultTable {
    std::vector<std::string> columns;
    std::vector<std::vector<ResultValue>> rows;
};

// Opciones del verificador.
struct VerifyOptions {
    // Solo C1 a C6: comprueba ademas el acumulador de cada nodo de cobertura,
    // como el procedimiento literal del articulo (ver selection.hpp).
    bool check_accumulators = false;
};

// Decision del verificador sobre una respuesta.
struct VerifierOutcome {
    bool accepted = false;
    std::string reason;   // motivo del rechazo (vacio si se acepto)
    std::string detail;   // resumen del resultado aceptado, por ejemplo "9822 filas (3 nodos de cobertura)"
    ResultTable result;   // resultado verificado (vacio si se rechazo)
    std::vector<std::pair<std::string, double>> stages;  // segundos por etapa, si el procedimiento los mide
    double seconds = 0;   // tiempo total de la verificacion
};

/*
 * Arboles de delta con los que se verifica la consulta. Sirve para avisar
 * que una consulta no esta soportada antes de pedir la respuesta al
 * probador (RNF05).
 *
 * Parametros:
 *   query   consulta ya leida
 *   digest  digest delta vigente
 *
 * Devuelve:
 *   Los AIT que intervienen, en el orden en que se verifican.
 *
 * Errores:
 *   Lanza UnsupportedQuery si alguno no existe en delta.
 */
std::vector<scheme::TreeSpec> required_trees(const ParsedQuery& query, const scheme::DatabaseDigest& digest);

/*
 * Verify de cualquier consulta del catalogo: decodifica los bytes con el
 * formato que corresponde a la consulta pedida (si el servidor envia otro
 * tipo, se rechaza), aplica la verificacion del nucleo y arma la tabla del
 * resultado.
 *
 * Parametros:
 *   digest      digest delta vigente
 *   public_key  clave publica cuya huella ya se comprobo contra delta
 *   query       la consulta que hizo el cliente (no la que diga la respuesta)
 *   bytes       respuesta (R, pi) recibida del probador
 *   options     opciones de verificacion
 *
 * Devuelve:
 *   accepted = true con el resultado verificado, o accepted = false con el
 *   motivo y sin filas.
 */
VerifierOutcome verify_query(const scheme::DatabaseDigest& digest, const aso::PublicKey& public_key,
                             const ParsedQuery& query, const std::vector<unsigned char>& bytes,
                             const VerifyOptions& options = {});

/*
 * Texto de un valor del resultado para mostrarlo: "NULL", el entero, el
 * decimal con seis cifras o el texto tal cual.
 *
 * Parametros:
 *   value  valor del resultado
 *
 * Devuelve:
 *   Su representacion de texto.
 */
std::string value_text(const ResultValue& value);

}  // namespace tt::query

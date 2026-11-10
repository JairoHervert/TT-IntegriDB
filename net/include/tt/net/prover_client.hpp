// prover_client.hpp
//
// Cliente de red del modulo verificador: envia una consulta al probador y
// recibe los bytes de la respuesta (R, pi). No interpreta esos bytes; eso lo
// hace query::verify_query, que los trata como no confiables.
//
// Errores que distingue:
//   - NetworkError: no se pudo conectar, se agoto el tiempo de espera, la
//     conexion se corto o la respuesta excede el tamano maximo.
//   - ServerError: el probador contesto, pero con un error (consulta mal
//     escrita, no soportada o falla del servidor). El mensaje del servidor
//     se muestra recortado y saneado.

#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt/net/protocol.hpp"

namespace tt::net {

// El probador respondio con un codigo de error.
class ServerError : public std::runtime_error {
public:
    /*
     * Parametros:
     *   status   codigo HTTP recibido
     *   message  mensaje ya saneado
     */
    ServerError(int status, const std::string& message) : std::runtime_error(message), status_(status) {}

    /*
     * Codigo HTTP que envio el probador (400, 413, 422, 500...).
     */
    int status() const { return status_; }

private:
    int status_;
};

// Opciones del cliente.
struct ClientOptions {
    int connect_timeout_seconds = 5;               // para establecer la conexion
    int read_timeout_seconds = 600;                // para esperar cada lectura de la respuesta
    std::size_t max_response_bytes = kMaxResponseBytes;  // respuesta mas grande que se acepta
};

// Tiempos que declara el probador mediante cabeceras HTTP. Son metadatos
// no autenticados: nunca se utilizan para decidir el resultado de Verify.
struct ServerTimings {
    double generation_seconds = 0;  // ProofGen, incluidas lecturas de D
    double encoding_seconds = 0;    // codificacion TTRSP
    double queue_seconds = 0;       // espera del candado en el servidor
    double handler_seconds = 0;     // manejo de la peticion, sin envio TCP
    std::optional<double> row_fetch_seconds;  // tiempo de lectura de filas de D
    // Las tres medidas se publican juntas; ausentes en consultas no instrumentadas.
    std::optional<double> ait_seconds;
    std::optional<double> merkle_seconds;
    std::optional<double> aso_seconds;
};

// Respuesta de una peticion medida desde el verificador. Si las cabeceras
// faltan o estan mal formadas, se conservan los bytes y se omiten los tiempos
// de servidor: solamente Verify puede aceptar o rechazar la respuesta.
// Metricas de la ruta experimental: tampoco se autentican criptograficamente.
struct DirectServerTimings {
    double sql_seconds = 0;
    double encoding_seconds = 0;
    double queue_seconds = 0;
    double handler_seconds = 0;
};

struct MeasuredDirect {
    std::string json;  // contenido SQL no verificado (JSON del servidor)
    double roundtrip_seconds = 0;
    std::optional<DirectServerTimings> server_timings;
};

/*
 * Solicita al servidor la referencia SQL directa, sin ejecutar Verify.
 * Solo se utiliza en la modalidad de evaluacion habilitada expresamente.
 */
MeasuredDirect fetch_direct_measured(const Endpoint& endpoint, const std::string& query_text,
                                     const ClientOptions& options = {});

struct MeasuredAnswer {
    std::vector<unsigned char> bytes;
    double roundtrip_seconds = 0;  // POST visto por el verificador
    std::optional<ServerTimings> server_timings;
};

/*
 * Envia la consulta y devuelve los bytes TTRSP con las mediciones disponibles.
 * El tiempo de ida y vuelta incluye conexion, servidor y transferencia.
 */
MeasuredAnswer fetch_answer_measured(const Endpoint& endpoint, const std::string& query_text,
                                     const ClientOptions& options = {});

/*
 * Envia la consulta al probador (POST /v1/query) y devuelve los bytes de la
 * respuesta.
 *
 * Parametros:
 *   endpoint    direccion del probador
 *   query_text  consulta en texto, por ejemplo "C4 6 8"
 *   options     tiempos de espera y tamano maximo
 *
 * Devuelve:
 *   Los bytes recibidos (sin revisar).
 *
 * Errores:
 *   Lanza NetworkError si no hubo comunicacion completa y ServerError si el
 *   probador respondio con un codigo distinto de 200.
 */
std::vector<unsigned char> fetch_answer(const Endpoint& endpoint, const std::string& query_text,
                                        const ClientOptions& options = {});

/*
 * Pide el estado del probador (GET /v1/health).
 *
 * Parametros:
 *   endpoint  direccion del probador
 *   options   tiempos de espera
 *
 * Devuelve:
 *   El estado que declara el probador. Es informativo: la verificacion de
 *   cada respuesta no depende de el.
 *
 * Errores:
 *   Lanza NetworkError, ServerError o std::runtime_error si el texto no
 *   tiene el formato esperado.
 */
HealthInfo fetch_health(const Endpoint& endpoint, const ClientOptions& options = {});

}  // namespace tt::net

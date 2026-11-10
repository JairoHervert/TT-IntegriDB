// prover_server.hpp
//
// Servidor de red del modulo probador (bloque Servidor). Recibe consultas
// por HTTP (ver protocol.hpp), las responde con query::answer_query y
// devuelve los bytes TTRSP.
//
// Decisiones:
//   - El estado autenticado, delta y la clave publica se cargan una sola vez
//     al iniciar el servidor (no en cada consulta, como en tt_prover answer).
//   - Las conexiones se atienden con un grupo pequeno de hilos, pero las
//     consultas se procesan de una en una: ProofGen es intensivo en
//     calculo, y asi el estado de NTL y la conexion a MySQL nunca se
//     comparten entre dos consultas a la vez. /v1/health se responde aunque
//     haya una consulta en curso.
//   - El servidor no confia en la peticion: limita su tamano, solo acepta
//     texto imprimible y la consulta se lee con el mismo parse_query
//     estricto que usan los programas.

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "tt/net/protocol.hpp"
#include "tt/query/prover_service.hpp"

namespace tt::net {

// Opciones del servidor.
struct ServerOptions {
    std::string host = "0.0.0.0";   // interfaz donde escucha (0.0.0.0: todas)
    int port = kDefaultPort;        // 0: el sistema elige un puerto libre (pruebas)
    std::size_t threads = 4;        // hilos para atender conexiones
    bool direct_benchmark = false;  // ruta /v1/direct optativa, deshabilitada por defecto
    int read_timeout_seconds = 10;  // tiempo maximo para recibir la peticion
    int write_timeout_seconds = 120;  // tiempo maximo por escritura de la respuesta
};

// Registro de una peticion atendida, para la bitacora del probador.
struct RequestRecord {
    std::string client;          // direccion del cliente
    std::string method;          // GET o POST
    std::string path;            // ruta pedida
    std::string query;           // texto de la consulta (recortado y saneado)
    int status = 0;              // codigo HTTP enviado
    std::size_t bytes = 0;       // tamano del cuerpo enviado
    double seconds = 0;             // procesamiento HTTP (excluye envio TCP)
    double queue_seconds = 0;       // espera del candado
    double generation_seconds = 0;  // ProofGen, incluidas lecturas de D
    double encoding_seconds = 0;    // codificacion TTRSP
    double row_fetch_seconds = 0;   // lectura de filas por RowStore
    std::size_t row_fetch_calls = 0; // llamadas a RowStore
    std::size_t row_fetch_rows = 0;  // filas obtenidas
    bool detailed_stages = false;     // disponibles solo para C8, C9 y C14
    double ait_seconds = 0;          // operaciones AIT instrumentadas
    double merkle_seconds = 0;       // hashes y pruebas Merkle
    double aso_seconds = 0;          // operaciones ASO instrumentadas
    std::string note;            // resumen de la respuesta o motivo del error
    std::size_t stale_rows = 0;  // filas de D que cambiaron despues de Setup
};

using RequestLogger = std::function<void(const RequestRecord&)>;

// El adaptador MySQL ejecuta una consulta del catalogo y devuelve JSON sin
// prueba criptografica. La biblioteca de red no depende de libmysqlclient.
struct DirectReply {
    std::string json;
    std::size_t rows = 0;
    double sql_seconds = 0;       // conexion MySQL, SQL y lectura de filas
    double encoding_seconds = 0;  // conversion a JSON
};
using DirectRunner = std::function<DirectReply(const query::ParsedQuery&)>;

// Servidor del probador. No se puede copiar.
class ProverServer {
public:
    /*
     * Prepara el servidor; todavia no abre el puerto.
     *
     * Parametros:
     *   context  estado autenticado, clave publica y origen de filas; los
     *            objetos a los que apunta deben vivir mas que el servidor
     *   options  interfaz, puerto, hilos y tiempos de espera
     *   logger   funcion que recibe cada peticion atendida (puede ser vacia)
     */
    ProverServer(query::ProverContext context, ServerOptions options, RequestLogger logger = {},
                 DirectRunner direct_runner = {});

    /*
     * Detiene el servidor si sigue escuchando.
     */
    ~ProverServer();

    ProverServer(const ProverServer&) = delete;
    ProverServer& operator=(const ProverServer&) = delete;

    /*
     * Abre el puerto. Con port = 0 el sistema elige uno libre.
     *
     * Devuelve:
     *   El puerto en el que quedo escuchando.
     *
     * Errores:
     *   Lanza NetworkError si el puerto esta ocupado o la interfaz no existe.
     */
    int bind();

    /*
     * Atiende peticiones hasta que se llame a stop(). Debe llamarse despues
     * de bind().
     *
     * Errores:
     *   Lanza NetworkError si el servidor no pudo seguir escuchando.
     */
    void run();

    /*
     * Espera a que run() este aceptando conexiones. Lo usan las pruebas, que
     * ejecutan run() en otro hilo.
     */
    void wait_until_ready() const;

    /*
     * Pide al servidor que deje de escuchar; run() regresa en cuanto
     * terminan las peticiones en curso. Se puede llamar desde otro hilo.
     */
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::net

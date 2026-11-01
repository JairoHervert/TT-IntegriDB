// portal.hpp
//
// Logica de la interfaz del caso de estudio (bloque Cliente): recibe las
// consultas que escribe el usuario en la pagina, las envia al probador por
// la red, verifica la respuesta con delta y la clave publica, y devuelve el
// veredicto y el resultado en JSON para que la pagina los muestre.
//
// La pagina no verifica nada: solo muestra lo que decidio query::verify_query
// aqui, en el mismo proceso que tiene delta y la clave publica. Por eso el
// servidor de la interfaz escucha por omision solo en 127.0.0.1 (ver
// ui_server.hpp).
//
// Esta clase no conoce HTTP: cada metodo recibe texto y devuelve el codigo
// y el cuerpo JSON. Asi se puede probar sin abrir puertos.
//
// Veredictos ("outcome" en el JSON):
//   verified     la respuesta paso Verify; incluye el resultado
//   rejected     la respuesta no paso Verify; incluye el motivo y no filas
//   no_answer    el probador no entrego respuesta (red o error del servidor)
//   invalid      la consulta esta mal escrita (no se envia al probador)
//   unsupported  delta no tiene los AIT que la consulta necesita (RNF05; no
//                se envia al probador)
//   ready        (solo en preview) la consulta se puede enviar

#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>

#include "tt/aso/keys.hpp"
#include "tt/net/prover_client.hpp"
#include "tt/net/protocol.hpp"
#include "tt/scheme/digest.hpp"

namespace tt::ui {

// Respuesta de la interfaz: codigo HTTP y cuerpo JSON.
struct ApiReply {
    int status = 200;
    std::string body;
};

// Registro de una consulta atendida, para la bitacora de tt_verifier ui.
struct QueryRecord {
    std::string query;          // texto de la consulta (recortado y saneado)
    std::string outcome;        // verified, rejected, no_answer, invalid, unsupported
    std::string note;           // resumen del resultado o motivo
    std::size_t bytes = 0;      // tamano de la respuesta del probador
    double prover_seconds = 0;  // envio + ProofGen + recepcion
    double verify_seconds = 0;  // Verify en este proceso
};

using QueryLogger = std::function<void(const QueryRecord&)>;

// Opciones de la interfaz.
struct PortalOptions {
    net::Endpoint prover;           // direccion del probador
    net::ClientOptions query;       // tiempos y limite para /v1/query
    net::ClientOptions health;      // tiempos para /v1/health (cortos)
    std::size_t max_rows = 200000;  // filas maximas que se envian a la pagina
};

// Interfaz del caso de estudio sobre un delta y una clave publica ya
// cargados (y cuya huella ya se comprobo).
class Portal {
public:
    /*
     * Parametros:
     *   digest      delta vigente; debe vivir mas que el Portal
     *   public_key  clave publica del ASO; debe vivir mas que el Portal
     *   options     direccion del probador, tiempos y limites
     *   logger      recibe cada consulta atendida (puede ser vacio)
     */
    Portal(const scheme::DatabaseDigest& digest, const aso::PublicKey& public_key, PortalOptions options,
           QueryLogger logger = {});

    /*
     * GET /api/status: estado local (id de delta, plan y columnas que admiten
     * consultas de rango) y estado del probador (si responde y si su delta
     * es el mismo).
     *
     * Devuelve:
     *   Siempre 200; si el probador no responde, "reachable" es false.
     */
    ApiReply status() const;

    /*
     * POST /api/preview: lee la consulta y dice como se verificaria (SQL y
     * AIT), sin enviarla al probador.
     *
     * Parametros:
     *   text  la consulta, por ejemplo "C4 6 8"
     *
     * Devuelve:
     *   200 con outcome "ready"; 400 si esta mal escrita; 422 si no esta
     *   soportada.
     */
    ApiReply preview(const std::string& text) const;

    /*
     * POST /api/query: envia la consulta al probador, verifica la respuesta y
     * devuelve el veredicto con el resultado.
     *
     * Parametros:
     *   text  la consulta, por ejemplo "C4 6 8"
     *
     * Devuelve:
     *   200 con outcome verified, rejected o no_answer; 400 si la consulta
     *   esta mal escrita; 422 si no esta soportada.
     */
    ApiReply query(const std::string& text);

private:
    const scheme::DatabaseDigest& digest_;
    const aso::PublicKey& public_key_;
    PortalOptions options_;
    QueryLogger logger_;
    std::string state_id_;       // id de delta en hexadecimal (64 digitos)
    std::mutex verify_mutex_;    // una verificacion a la vez (estado de NTL por hilo)
};

}  // namespace tt::ui

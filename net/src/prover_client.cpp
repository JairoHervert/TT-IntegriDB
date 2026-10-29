// prover_client.cpp

#include "tt/net/prover_client.hpp"

#include <string>

#include "httplib.h"

namespace tt::net {

namespace {

// Respuesta de error mas larga que se lee del servidor (solo es texto).
constexpr std::size_t kMaxErrorBytes = 4096;

/*
 * Crea el cliente HTTP con los tiempos de espera y el limite de tamano.
 */
httplib::Client make_client(const Endpoint& endpoint, const ClientOptions& options, std::size_t max_bytes) {
    httplib::Client cli(endpoint.host, endpoint.port);
    cli.set_connection_timeout(options.connect_timeout_seconds, 0);
    cli.set_read_timeout(options.read_timeout_seconds, 0);
    cli.set_write_timeout(10, 0);
    cli.set_payload_max_length(max_bytes);
    cli.set_keep_alive(false);
    return cli;
}

/*
 * Traduce un fallo de la biblioteca HTTP a NetworkError con un mensaje que
 * explique la causa mas probable.
 */
[[noreturn]] void throw_network_error(const Endpoint& endpoint, httplib::Error error, std::size_t max_bytes) {
    std::string cause;
    switch (error) {
        case httplib::Error::Connection:
            cause = "no se pudo conectar (el probador no esta en ejecucion, la direccion es incorrecta o un "
                    "firewall lo impide)";
            break;
        case httplib::Error::ConnectionTimeout:
            cause = "se agoto el tiempo para conectar";
            break;
        case httplib::Error::Timeout:
        case httplib::Error::Read:
            cause = "se agoto el tiempo de espera o se corto la conexion mientras llegaba la respuesta";
            break;
        case httplib::Error::ExceedMaxPayloadSize:
            cause = "la respuesta excede el limite de " + std::to_string(max_bytes / (1024 * 1024)) + " MB";
            break;
        default:
            cause = httplib::to_string(error);
            break;
    }
    throw NetworkError("probador en " + endpoint.text() + ": " + cause);
}

/*
 * Convierte una respuesta con codigo de error en ServerError.
 */
[[noreturn]] void throw_server_error(const Endpoint& endpoint, int status, const std::string& body) {
    std::string trimmed = body;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' ')) {
        trimmed.pop_back();
    }
    std::string message = safe_text(trimmed);
    if (message.empty()) message = "sin mensaje";
    throw ServerError(status, "el probador en " + endpoint.text() + " respondio " + std::to_string(status) + ": " +
                                  message);
}

}  // namespace

/*
 * Implementacion: POST con el texto de la consulta; solo el codigo 200
 * entrega bytes.
 */
std::vector<unsigned char> fetch_answer(const Endpoint& endpoint, const std::string& query_text,
                                        const ClientOptions& options) {
    httplib::Client cli = make_client(endpoint, options, options.max_response_bytes);
    httplib::Result res = cli.Post(kQueryPath, query_text, "text/plain");
    if (!res) throw_network_error(endpoint, res.error(), options.max_response_bytes);
    if (res->status != 200) throw_server_error(endpoint, res->status, res->body.substr(0, kMaxErrorBytes));
    return std::vector<unsigned char>(res->body.begin(), res->body.end());
}

/*
 * Implementacion: GET con un limite pequeno, porque el estado ocupa unas
 * cuantas lineas.
 */
HealthInfo fetch_health(const Endpoint& endpoint, const ClientOptions& options) {
    httplib::Client cli = make_client(endpoint, options, kMaxErrorBytes);
    httplib::Result res = cli.Get(kHealthPath);
    if (!res) throw_network_error(endpoint, res.error(), kMaxErrorBytes);
    if (res->status != 200) throw_server_error(endpoint, res->status, res->body);
    return health_from_text(res->body);
}

}  // namespace tt::net

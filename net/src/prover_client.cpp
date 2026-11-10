// prover_client.cpp

#include "tt/net/prover_client.hpp"

#include <chrono>
#include <cmath>
#include <exception>
#include <string>
#include <utility>

#include "httplib.h"

namespace tt::net {

namespace {

// Respuesta de error mas larga que se lee del servidor (solo es texto).
constexpr std::size_t kMaxErrorBytes = 4096;

/*
 * Lee una duracion opcional de HTTP. Como el servidor no es de confianza,
 * solo admite decimales ASCII finitos y no negativos, de hasta 24 caracteres
 * y 9 decimales, con un maximo de 86400 segundos.
 */
bool metric_value(const httplib::Response& res, const char* name, double& value) {
    if (res.get_header_value_count(name) != 1) return false;
    const std::string raw = res.get_header_value(name);
    if (raw.empty() || raw.size() > 24) return false;
    std::size_t dot_count = 0;
    std::size_t decimals = 0;
    for (char c : raw) {
        if (c == '.') {
            if (++dot_count != 1) return false;
        } else if (c >= '0' && c <= '9') {
            if (dot_count) ++decimals;
        } else {
            return false;
        }
    }
    if (raw.front() == '.' || raw.back() == '.' || decimals > 9) return false;
    try {
        const double parsed = std::stod(raw);
        if (!std::isfinite(parsed) || parsed < 0 || parsed > 86400) return false;
        value = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

/*
 * Lee la version 1 completa. Si falta alguna cabecera o los tiempos son
 * incoherentes, se omite el bloque sin modificar la respuesta TTRSP.
 */
std::optional<ServerTimings> parse_server_timings(const httplib::Response& res) {
    if (res.get_header_value_count("X-TT-Metrics-Version") != 1 ||
        res.get_header_value("X-TT-Metrics-Version") != "1") return std::nullopt;
    ServerTimings m;
    if (!metric_value(res, "X-TT-Generation-Seconds", m.generation_seconds) ||
        !metric_value(res, "X-TT-Encoding-Seconds", m.encoding_seconds) ||
        !metric_value(res, "X-TT-Queue-Seconds", m.queue_seconds) ||
        !metric_value(res, "X-TT-Handler-Seconds", m.handler_seconds)) return std::nullopt;
    // Tolerancia de redondeo de 9 decimales y pequenas fronteras de reloj.
    if (m.generation_seconds + m.encoding_seconds > m.handler_seconds + 0.01 ||
        m.queue_seconds > m.handler_seconds + 0.01) return std::nullopt;
    double fetch_seconds = 0;
    if (metric_value(res, "X-TT-Row-Fetch-Seconds", fetch_seconds) &&
        fetch_seconds <= m.generation_seconds + 0.01) {
        m.row_fetch_seconds = fetch_seconds;
    }
    // Cabeceras optativas de diagnostico; un valor inventado o incompleto
    // nunca modifica los bytes ni el resultado de Verify.
    const bool any_stage = res.get_header_value_count("X-TT-AIT-Seconds") != 0 ||
                           res.get_header_value_count("X-TT-Merkle-Seconds") != 0 ||
                           res.get_header_value_count("X-TT-ASO-Seconds") != 0;
    if (any_stage) {
        double ait = 0, merkle = 0, aso = 0;
        if (metric_value(res, "X-TT-AIT-Seconds", ait) &&
            metric_value(res, "X-TT-Merkle-Seconds", merkle) &&
            metric_value(res, "X-TT-ASO-Seconds", aso)) {
            const double accounted = ait + merkle + aso + m.row_fetch_seconds.value_or(0);
            if (accounted <= m.generation_seconds + 0.01) {
                m.ait_seconds = ait;
                m.merkle_seconds = merkle;
                m.aso_seconds = aso;
            }
        }
    }
    return m;
}

/*
 * Lee los cuatro tiempos de la ruta SQL directa. Son informativos y
 * opcionales; no se permite inferirlos si el servidor no los envio.
 */
std::optional<DirectServerTimings> parse_direct_timings(const httplib::Response& res) {
    DirectServerTimings m;
    if (!metric_value(res, "X-TT-Direct-Sql-Seconds", m.sql_seconds) ||
        !metric_value(res, "X-TT-Direct-Encoding-Seconds", m.encoding_seconds) ||
        !metric_value(res, "X-TT-Direct-Queue-Seconds", m.queue_seconds) ||
        !metric_value(res, "X-TT-Direct-Handler-Seconds", m.handler_seconds)) return std::nullopt;
    if (m.sql_seconds + m.encoding_seconds > m.handler_seconds + 0.01 ||
        m.queue_seconds > m.handler_seconds + 0.01) return std::nullopt;
    return m;
}

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
MeasuredAnswer fetch_answer_measured(const Endpoint& endpoint, const std::string& query_text,
                                     const ClientOptions& options) {
    httplib::Client cli = make_client(endpoint, options, options.max_response_bytes);
    const auto start = std::chrono::steady_clock::now();
    httplib::Result res = cli.Post(kQueryPath, query_text, "text/plain");
    if (!res) throw_network_error(endpoint, res.error(), options.max_response_bytes);
    if (res->status != 200) throw_server_error(endpoint, res->status, res->body.substr(0, kMaxErrorBytes));
    MeasuredAnswer result;
    result.roundtrip_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.server_timings = parse_server_timings(*res);
    result.bytes.assign(res->body.begin(), res->body.end());
    return result;
}

/*
 * El recorrido SQL directo usa el mismo enlace HTTP y limites que TTRSP,
 * pero no interpreta su cuerpo como una prueba verificable.
 */
MeasuredDirect fetch_direct_measured(const Endpoint& endpoint, const std::string& query_text,
                                     const ClientOptions& options) {
    httplib::Client cli = make_client(endpoint, options, options.max_response_bytes);
    const auto start = std::chrono::steady_clock::now();
    httplib::Result res = cli.Post(kDirectPath, query_text, "text/plain");
    if (!res) throw_network_error(endpoint, res.error(), options.max_response_bytes);
    if (res->status != 200) throw_server_error(endpoint, res->status, res->body.substr(0, kMaxErrorBytes));
    MeasuredDirect result;
    result.roundtrip_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.server_timings = parse_direct_timings(*res);
    result.json = std::move(res->body);
    return result;
}

/*
 * Mantiene la interfaz anterior del cliente y entrega los mismos bytes.
 */
std::vector<unsigned char> fetch_answer(const Endpoint& endpoint, const std::string& query_text,
                                        const ClientOptions& options) {
    return fetch_answer_measured(endpoint, query_text, options).bytes;
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

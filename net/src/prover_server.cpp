// prover_server.cpp

#include "tt/net/prover_server.hpp"

#include <chrono>
#include <exception>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "httplib.h"
#include "tt/crypto/pairing_group.hpp"
#include "tt/query/query.hpp"

namespace tt::net {

namespace {

using Clock = std::chrono::steady_clock;

/*
 * Segundos transcurridos desde un instante del reloj monotono.
 */
double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/*
 * Indica si el cuerpo de la peticion solo tiene ASCII imprimible y espacios.
 */
bool plain_text(const std::string& body) {
    for (char c : body) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(u >= 0x20 && u < 0x7f) && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}

/*
 * Mensaje de texto para los errores que genera la propia biblioteca HTTP
 * (ruta inexistente, metodo no permitido, peticion demasiado grande...).
 */
std::string library_error_text(int status) {
    switch (status) {
        case 404: return "ruta desconocida; las rutas del probador son POST /v1/query y GET /v1/health";
        case 405: return "metodo no permitido en esta ruta";
        case 413: return "la consulta excede " + std::to_string(kMaxQueryBytes) + " bytes";
        default: return "peticion no valida";
    }
}

}  // namespace

// Estado interno del servidor; separa a quien incluye prover_server.hpp de
// la biblioteca HTTP.
struct ProverServer::Impl {
    query::ProverContext context;
    ServerOptions options;
    RequestLogger logger;
    httplib::Server server;
    std::mutex work_mutex;     // una consulta a la vez (ver prover_server.hpp)
    std::string health_text;   // cuerpo fijo de /v1/health

    /*
     * Envia un registro a la bitacora, si hay quien lo reciba.
     */
    void log(const RequestRecord& record) const {
        if (logger) logger(record);
    }

    /*
     * Atiende POST /v1/query: valida el texto, lee la consulta y la responde
     * con answer_query, traduciendo cada error a su codigo HTTP.
     */
    void handle_query(const httplib::Request& req, httplib::Response& res) {
        const Clock::time_point start = Clock::now();
        RequestRecord record;
        record.client = req.remote_addr + ":" + std::to_string(req.remote_port);
        record.method = req.method;
        record.path = req.path;
        record.query = safe_text(req.body, 120);
        auto fail = [&](int status, const std::string& message) {
            res.status = status;
            res.set_content(message + "\n", "text/plain; charset=utf-8");
            record.status = status;
            record.note = message;
        };
        try {
            if (!plain_text(req.body)) {
                fail(400, "la consulta debe ser texto ASCII imprimible");
            } else {
                const std::vector<std::string> words = split_words(req.body);
                const query::ParsedQuery parsed = query::parse_query(words);
                std::lock_guard<std::mutex> lock(work_mutex);
                // El modulo de NTL es por hilo: cada hilo que atiende una
                // consulta debe fijarlo antes de usar el grupo.
                crypto::init_pairing_group();
                query::ProverReply reply = query::answer_query(context, parsed);
                res.status = 200;
                res.set_content(std::string(reply.bytes.begin(), reply.bytes.end()), "application/octet-stream");
                record.status = 200;
                record.note = reply.summary;
                record.stale_rows = reply.stale_rows;
            }
        } catch (const query::QueryError& e) {
            fail(400, std::string("consulta mal escrita: ") + e.what());
        } catch (const query::UnsupportedQuery& e) {
            fail(422, std::string("consulta no soportada: ") + e.what());
        } catch (const std::exception& e) {
            fail(500, std::string("el probador no pudo responder: ") + e.what());
        }
        record.bytes = res.body.size();
        record.seconds = seconds_since(start);
        log(record);
    }

    /*
     * Atiende GET /v1/health con el estado cargado al iniciar.
     */
    void handle_health(const httplib::Request& req, httplib::Response& res) const {
        res.status = 200;
        res.set_content(health_text, "text/plain; charset=utf-8");
        RequestRecord record;
        record.client = req.remote_addr + ":" + std::to_string(req.remote_port);
        record.method = req.method;
        record.path = req.path;
        record.status = 200;
        record.bytes = res.body.size();
        log(record);
    }

    /*
     * Completa los errores que genera la biblioteca (cuerpo vacio) con un
     * mensaje y los registra; los de handle_query ya traen su mensaje.
     */
    void handle_error(const httplib::Request& req, httplib::Response& res) const {
        if (!res.body.empty()) return;
        const std::string message = library_error_text(res.status);
        res.set_content(message + "\n", "text/plain; charset=utf-8");
        RequestRecord record;
        record.client = req.remote_addr + ":" + std::to_string(req.remote_port);
        record.method = req.method;
        record.path = safe_text(req.path, 120);
        record.status = res.status;
        record.bytes = res.body.size();
        record.note = message;
        log(record);
    }
};

/*
 * Implementacion: configura limites, hilos y rutas; el puerto se abre en
 * bind().
 */
ProverServer::ProverServer(query::ProverContext context, ServerOptions options, RequestLogger logger)
    : impl_(std::make_unique<Impl>()) {
    if (context.database == nullptr || context.digest == nullptr) {
        throw std::invalid_argument("el servidor del probador necesita delta y D~ cargados");
    }
    impl_->context = context;
    impl_->options = std::move(options);
    impl_->logger = std::move(logger);

    HealthInfo info;
    info.state_id = context.digest->id();
    info.state_version = context.digest->state_version;
    info.plan = context.digest->plan;
    info.public_key = context.public_key != nullptr;
    impl_->health_text = health_to_text(info);

    httplib::Server& svr = impl_->server;
    const std::size_t threads = impl_->options.threads == 0 ? 1 : impl_->options.threads;
    svr.new_task_queue = [threads] { return new httplib::ThreadPool(threads); };
    svr.set_payload_max_length(kMaxQueryBytes);
    svr.set_read_timeout(impl_->options.read_timeout_seconds, 0);
    svr.set_write_timeout(impl_->options.write_timeout_seconds, 0);

    Impl* impl = impl_.get();
    svr.Post(kQueryPath, [impl](const httplib::Request& req, httplib::Response& res) { impl->handle_query(req, res); });
    svr.Get(kHealthPath,
            [impl](const httplib::Request& req, httplib::Response& res) { impl->handle_health(req, res); });
    svr.set_error_handler([impl](const httplib::Request& req, httplib::Response& res) { impl->handle_error(req, res); });
}

/*
 * Implementacion: stop() no hace nada si el servidor no esta escuchando.
 */
ProverServer::~ProverServer() { stop(); }

/*
 * Implementacion: con puerto 0 se usa bind_to_any_port.
 */
int ProverServer::bind() {
    httplib::Server& svr = impl_->server;
    const ServerOptions& o = impl_->options;
    int port = o.port;
    if (port == 0) {
        port = svr.bind_to_any_port(o.host);
    } else if (!svr.bind_to_port(o.host, port)) {
        port = -1;
    }
    if (port <= 0) {
        throw NetworkError("no se pudo abrir el puerto " + std::to_string(o.port) + " en " + o.host +
                           " (puede estar ocupado o la interfaz no existe)");
    }
    return port;
}

/*
 * Implementacion: listen_after_bind bloquea hasta stop().
 */
void ProverServer::run() {
    if (!impl_->server.listen_after_bind()) {
        throw NetworkError("el servidor del probador dejo de escuchar por un error de red");
    }
}

/*
 * Implementacion: delega en la biblioteca.
 */
void ProverServer::wait_until_ready() const { impl_->server.wait_until_ready(); }

/*
 * Implementacion: delega en la biblioteca, que es segura entre hilos.
 */
void ProverServer::stop() {
    if (impl_ && impl_->server.is_running()) impl_->server.stop();
}

}  // namespace tt::net

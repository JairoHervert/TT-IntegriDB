// ui_server.cpp

#include "tt/ui/ui_server.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "httplib.h"
#include "tt/net/protocol.hpp"
#include "tt/ui/json.hpp"

namespace tt::ui {

namespace {

// Politica de contenido de la pagina: scripts, estilos, imagenes y llamadas
// solo del mismo origen; nada de marcos ni formularios hacia otros sitios.
constexpr const char* kContentSecurityPolicy =
    "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; "
    "base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

/*
 * Indica si la ruta pertenece a las llamadas de la pagina (/api/...).
 */
bool is_api(const std::string& path) { return path.rfind("/api/", 0) == 0; }

/*
 * Cuerpo JSON de un error del servidor de la interfaz.
 */
std::string error_json(const std::string& message) {
    JsonWriter w;
    w.begin_object().key("outcome").value("error").key("message").value(message).end_object();
    return w.str();
}

/*
 * Mensaje para los errores que genera la biblioteca HTTP o el propio
 * servidor antes de llegar a una ruta.
 */
std::string error_text(int status) {
    switch (status) {
        case 403: return std::string("falta la cabecera ") + kUiHeader + "; las consultas solo se aceptan desde la pagina";
        case 404: return "ruta desconocida";
        case 405: return "metodo no permitido en esta ruta";
        case 413: return "la consulta excede " + std::to_string(net::kMaxQueryBytes) + " bytes";
        default: return "peticion no valida";
    }
}

}  // namespace

// Estado interno; separa a quien incluye ui_server.hpp de la biblioteca HTTP.
struct UiServer::Impl {
    Portal* portal = nullptr;
    UiServerOptions options;
    HttpErrorLogger errors;
    httplib::Server server;

    /*
     * Escribe la respuesta de una llamada de la interfaz.
     */
    static void reply(httplib::Response& res, const ApiReply& r) {
        res.status = r.status;
        res.set_content(r.body, "application/json; charset=utf-8");
    }

    /*
     * Envia a la bitacora una peticion que termino en error.
     */
    void log_error(const httplib::Request& req, int status) const {
        if (!errors) return;
        HttpErrorRecord record;
        record.client = req.remote_addr + ":" + std::to_string(req.remote_port);
        record.method = net::safe_text(req.method, 10);
        record.path = net::safe_text(req.path, 120);
        record.status = status;
        errors(record);
    }
};

/*
 * Implementacion: monta la carpeta de la pagina, registra las rutas y fija
 * limites; el puerto se abre en bind().
 */
UiServer::UiServer(Portal& portal, UiServerOptions options, HttpErrorLogger errors)
    : impl_(std::make_unique<Impl>()) {
    impl_->portal = &portal;
    impl_->options = std::move(options);
    impl_->errors = std::move(errors);

    const std::filesystem::path index = impl_->options.web_dir / "index.html";
    if (!std::filesystem::is_regular_file(index)) {
        throw std::invalid_argument("no se encontro la pagina de la interfaz (" + index.string() +
                                    "); indica la carpeta ui/web con --web");
    }

    httplib::Server& svr = impl_->server;
    const std::size_t threads = impl_->options.threads == 0 ? 1 : impl_->options.threads;
    svr.new_task_queue = [threads] { return new httplib::ThreadPool(threads); };
    svr.set_payload_max_length(net::kMaxQueryBytes);
    svr.set_read_timeout(10, 0);
    svr.set_write_timeout(60, 0);
    svr.set_file_extension_and_mimetype_mapping("html", "text/html; charset=utf-8");
    svr.set_file_extension_and_mimetype_mapping("js", "text/javascript; charset=utf-8");
    svr.set_file_extension_and_mimetype_mapping("css", "text/css; charset=utf-8");
    svr.set_file_extension_and_mimetype_mapping("svg", "image/svg+xml");
    if (!svr.set_mount_point("/", impl_->options.web_dir.string())) {
        throw std::invalid_argument("no se pudo usar la carpeta " + impl_->options.web_dir.string());
    }

    Impl* impl = impl_.get();

    // Las llamadas POST sin la cabecera de la pagina se rechazan antes de
    // leer su cuerpo.
    svr.set_pre_routing_handler([impl](const httplib::Request& req, httplib::Response& res) {
        if (req.method == "POST" && is_api(req.path) && !req.has_header(kUiHeader)) {
            res.status = 403;
            res.set_content(error_json(error_text(403)), "application/json; charset=utf-8");
            impl->log_error(req, 403);
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Get("/api/status", [impl](const httplib::Request&, httplib::Response& res) {
        Impl::reply(res, impl->portal->status());
    });
    svr.Post("/api/preview", [impl](const httplib::Request& req, httplib::Response& res) {
        Impl::reply(res, impl->portal->preview(req.body));
    });
    svr.Post("/api/query", [impl](const httplib::Request& req, httplib::Response& res) {
        Impl::reply(res, impl->portal->query(req.body));
    });

    // Errores que no vienen de una ruta (404, 405, 413...): JSON para las
    // llamadas de la pagina y texto para lo demas.
    svr.set_error_handler([impl](const httplib::Request& req, httplib::Response& res) {
        if (!res.body.empty()) return;
        if (is_api(req.path)) {
            res.set_content(error_json(error_text(res.status)), "application/json; charset=utf-8");
        } else {
            res.set_content(error_text(res.status) + "\n", "text/plain; charset=utf-8");
        }
        impl->log_error(req, res.status);
    });

    // Cabeceras de seguridad en todas las respuestas; las llamadas de la
    // interfaz nunca se guardan en cache.
    svr.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Security-Policy", kContentSecurityPolicy);
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Referrer-Policy", "no-referrer");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("Cache-Control", is_api(req.path) ? "no-store" : "no-cache");
    });
}

/*
 * Implementacion: stop() no hace nada si el servidor no esta escuchando.
 */
UiServer::~UiServer() { stop(); }

/*
 * Implementacion: con puerto 0 se usa bind_to_any_port.
 */
int UiServer::bind() {
    httplib::Server& svr = impl_->server;
    const UiServerOptions& o = impl_->options;
    int port = o.port;
    if (port == 0) {
        port = svr.bind_to_any_port(o.host);
    } else if (!svr.bind_to_port(o.host, port)) {
        port = -1;
    }
    if (port <= 0) {
        throw net::NetworkError("no se pudo abrir el puerto " + std::to_string(o.port) + " en " + o.host +
                                " (puede estar ocupado o la interfaz no existe)");
    }
    return port;
}

/*
 * Implementacion: listen_after_bind bloquea hasta stop().
 */
void UiServer::run() {
    if (!impl_->server.listen_after_bind()) {
        throw net::NetworkError("el servidor de la interfaz dejo de escuchar por un error de red");
    }
}

/*
 * Implementacion: delega en la biblioteca.
 */
void UiServer::wait_until_ready() const { impl_->server.wait_until_ready(); }

/*
 * Implementacion: delega en la biblioteca, que es segura entre hilos.
 */
void UiServer::stop() {
    if (impl_ && impl_->server.is_running()) impl_->server.stop();
}

}  // namespace tt::ui

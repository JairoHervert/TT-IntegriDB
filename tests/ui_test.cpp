// ui_test.cpp
//
// Pruebas de la interfaz del caso de estudio (biblioteca tt_ui) sin MySQL:
// el probador y el servidor de la interfaz se levantan en este mismo
// proceso, en puertos libres de 127.0.0.1, con la base pequena en memoria.
//   - Escritor de JSON.
//   - Portal: estado, vista previa y las 14 formas del catalogo pasando por
//     el probador y por Verify, con el resultado igual al oraculo.
//   - Veredictos que no son "verified": probador con otro estado
//     (rechazada), probador sin clave publica o detenido (sin respuesta),
//     consulta mal escrita o no soportada (no se envia al probador).
//   - Servidor HTTP: pagina, cabeceras de seguridad, cabecera X-TT-UI,
//     limite de tamano y rutas fuera de la carpeta de la pagina.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "httplib.h"
#include "service_fixture.hpp"
#include "test_support.hpp"
#include "tt/net/prover_server.hpp"
#include "tt/net/protocol.hpp"
#include "tt/ui/json.hpp"
#include "tt/ui/portal.hpp"
#include "tt/ui/ui_server.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

namespace {

namespace fs = std::filesystem;

// Un servidor del probador ejecutandose en su propio hilo.
class RunningProver {
public:
    /*
     * Abre un puerto libre en 127.0.0.1 y empieza a atender en otro hilo.
     */
    RunningProver(query::ProverContext context, net::RequestLogger logger = {},
                  bool allow_direct = false, net::DirectRunner direct = {}) {
        net::ServerOptions options;
        options.direct_benchmark = allow_direct;
        options.host = "127.0.0.1";
        options.port = 0;
        server_ = std::make_unique<net::ProverServer>(context, options, std::move(logger), std::move(direct));
        port_ = server_->bind();
        thread_ = std::thread([this] { server_->run(); });
        server_->wait_until_ready();
    }

    /*
     * Detiene el servidor y espera a que termine su hilo.
     */
    ~RunningProver() { stop(); }

    /*
     * Detiene el servidor (se puede llamar mas de una vez).
     */
    void stop() {
        if (thread_.joinable()) {
            server_->stop();
            thread_.join();
        }
    }

    /*
     * Direccion del servidor.
     */
    net::Endpoint endpoint() const { return net::Endpoint{"127.0.0.1", port_}; }

private:
    std::unique_ptr<net::ProverServer> server_;
    std::thread thread_;
    int port_ = 0;
};

// Un servidor de la interfaz ejecutandose en su propio hilo.
class RunningUi {
public:
    /*
     * Abre un puerto libre en 127.0.0.1 con la pagina de web_dir.
     */
    RunningUi(ui::Portal& portal, const fs::path& web_dir) {
        ui::UiServerOptions options;
        options.port = 0;
        options.web_dir = web_dir;
        server_ = std::make_unique<ui::UiServer>(portal, options);
        port_ = server_->bind();
        thread_ = std::thread([this] { server_->run(); });
        server_->wait_until_ready();
    }

    /*
     * Detiene el servidor y espera a que termine su hilo.
     */
    ~RunningUi() {
        server_->stop();
        if (thread_.joinable()) thread_.join();
    }

    /*
     * Puerto abierto.
     */
    int port() const { return port_; }

private:
    std::unique_ptr<ui::UiServer> server_;
    std::thread thread_;
    int port_ = 0;
};

/*
 * Opciones del Portal para un probador en el puerto dado.
 */
ui::PortalOptions portal_options(const net::Endpoint& prover) {
    ui::PortalOptions o;
    o.prover = prover;
    o.health.connect_timeout_seconds = 2;
    o.health.read_timeout_seconds = 5;
    return o;
}

/*
 * Indica si el texto contiene el fragmento.
 */
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

/*
 * Texto de una consulta a partir de sus palabras.
 */
std::string text_of(const std::vector<std::string>& words) { return net::join_words(words); }

/*
 * Fragmento JSON que debe aparecer en la respuesta del Portal segun el
 * oraculo: el numero de filas o, para un agregado, el valor.
 */
std::string expected_fragment(const test::Expected& e) {
    if (!e.scalar) return "\"row_count\":" + std::to_string(e.rows.size()) + ",";
    if (std::holds_alternative<std::monostate>(e.value)) return "\"rows\":[[null]]";
    if (const auto* i = std::get_if<std::int64_t>(&e.value)) return "\"rows\":[[" + std::to_string(*i) + "]]";
    char text[40];
    std::snprintf(text, sizeof text, "%.15g", std::get<double>(e.value));
    return "\"rows\":[[" + std::string(text) + "]]";
}

/*
 * Crea una carpeta temporal con una pagina de prueba y, junto a ella (fuera
 * de la carpeta de la pagina), un archivo que no se debe poder leer.
 */
fs::path make_web_dir() {
    const fs::path root = fs::temp_directory_path() / ("tt_ui_test_" + std::to_string(::getpid()));
    fs::create_directories(root / "web");
    std::ofstream(root / "web" / "index.html") << "<!doctype html><title>prueba</title><p>pagina-de-prueba</p>\n";
    std::ofstream(root / "web" / "app.js") << "'use strict';\n";
    std::ofstream(root / "secret.txt") << "no-debe-salir\n";
    return root;
}

}  // namespace

/*
 * Ejecuta las comprobaciones y devuelve 0 si todas pasan.
 */
int main() {
    section("Escritor de JSON");
    {
        check(ui::json_escape("a\"b\\c\n\x01") == "a\\\"b\\\\c\\n\\u0001", "escapa comillas, diagonal y control");
        check(ui::json_escape("Programaci\xc3\xb3n") == "Programaci\xc3\xb3n", "deja pasar UTF-8");
        ui::JsonWriter w;
        w.begin_object().key("a").value(1).key("b").begin_array().value("x").null().value(true).end_array();
        w.key("c").value(0.25).key("d").begin_object().end_object().end_object();
        check(w.str() == "{\"a\":1,\"b\":[\"x\",null,true],\"c\":0.25,\"d\":{}}", "objetos, arreglos y comas");
        ui::JsonWriter n;
        n.begin_array().value(std::numeric_limits<double>::quiet_NaN()).value(std::numeric_limits<std::int64_t>::min());
        n.value(std::uint64_t{18446744073709551615u}).end_array();
        check(n.str() == "[null,-9223372036854775808,18446744073709551615]", "NaN como null y enteros extremos");
    }

    test::ServiceFixture fx;
    std::atomic<int> prover_requests{0};
    RunningProver prover(fx.context(), [&](const net::RequestRecord& r) {
        if (r.path == net::kQueryPath) ++prover_requests;
    });
    std::vector<ui::QueryRecord> records;
    ui::Portal portal(fx.state.digest, fx.keys.pk.aso, portal_options(prover.endpoint()),
                      [&](const ui::QueryRecord& r) { records.push_back(r); });

    section("Estado y vista previa");
    {
        const ui::ApiReply s = portal.status();
        check(s.status == 200 && has(s.body, "\"reachable\":true") && has(s.body, "\"matches\":true"),
              "/api/status: el probador responde con el mismo delta");
        check(has(s.body, "{\"table\":\"academic_record\",\"column\":\"grade\"}") &&
                  !has(s.body, "{\"table\":\"academic_record\",\"column\":\"course_id\"}"),
              "/api/status publica las columnas que admiten rangos");
        const ui::ApiReply ok = portal.preview("C4 6 8");
        check(ok.status == 200 && has(ok.body, "\"outcome\":\"ready\"") &&
                  has(ok.body, "\"trees\":[\"academic_record(grade -> record_id)\"]") &&
                  has(ok.body, "BETWEEN 6 AND 8"),
              "vista previa de C4: SQL y AIT");
        check(portal.preview("C4 8 6").status == 400, "rango invertido: 400");
        check(portal.preview("C4 6\x01 8").status == 400, "caracteres de control: 400");
        const ui::ApiReply un = portal.preview("range student program_id 1 2");
        check(un.status == 422 && has(un.body, "\"outcome\":\"unsupported\""), "sin AIT en delta: 422");
        check(prover_requests.load() == 0, "la vista previa nunca consulta al probador");
    }

    section("Las 14 formas del catalogo pasan por el probador y por Verify");
    for (const test::CatalogCase& c : test::catalog_cases()) {
        const ui::ApiReply r = portal.query(text_of(c.words));
        const std::string fragment = expected_fragment(test::oracle(fx.base, c.words));
        check(r.status == 200 && has(r.body, "\"outcome\":\"verified\"") && has(r.body, fragment),
              text_of(c.words) + ": verificada y con " + fragment);
    }
    check(!records.empty() && records.back().outcome == "verified" && records.back().bytes > 0,
          "la bitacora recibe cada consulta con su tamano");

    section("Comparacion SQL directa claramente no verificada");
    {
        check(portal.direct("C2 1").status == 403 &&
                  has(portal.status().body, "\"direct_benchmark_enabled\":false"),
              "SQL directo bloqueado de forma predeterminada");
        net::DirectRunner fake = [](const query::ParsedQuery&) {
            net::DirectReply reply;
            reply.json = "{\"outcome\":\"direct\",\"unverified\":true,\"result\":{\"columns\":[\"x\"],\"row_count\":1,\"truncated\":false,\"rows\":[[6]]}}";
            reply.rows = 1;
            reply.sql_seconds = 0.001;
            reply.encoding_seconds = 0.0001;
            return reply;
        };
        RunningProver benchmark(fx.context(), {}, true, fake);
        ui::PortalOptions options = portal_options(benchmark.endpoint());
        options.direct_benchmark = true;
        ui::Portal p(fx.state.digest, fx.keys.pk.aso, options);
        const ui::ApiReply direct = p.direct("C2 1");
        check(direct.status == 200 && has(direct.body, "\"unverified\":true") &&
                  !has(direct.body, "\"outcome\":\"verified\"") && !direct.headers.empty(),
              "la modalidad directa devuelve filas SIN sello ni Verify y expone sus tiempos");
        check(p.direct("C99 1").status == 400 && p.direct("range academic_record grade 6 8").status == 422,
              "consulta directa invalida o fuera de catalogo se rechaza");
    }

    section("Metricas de la consulta en el Portal");
    {
        const ui::ApiReply r = portal.query("C14 1");
        check(has(r.body, "\"outcome\":\"verified\"") && has(r.body, "\"timings\":{") &&
                  has(r.body, "\"generation_seconds\":") && has(r.body, "\"encoding_seconds\":") &&
                  has(r.body, "\"queue_seconds\":") && has(r.body, "\"handler_seconds\":") &&
                  has(r.body, "\"network_overhead_seconds\":") && has(r.body, "\"client_total_seconds\":") &&
                  has(r.body, "\"verification\":{") && has(r.body, "\"result\":"),
              "el Portal conserva el resultado verificado y muestra las etapas de tiempo");
        check(records.back().query == "C14 1" && records.back().prover_seconds >= 0 &&
                  records.back().verify_seconds >= 0,
              "la bitacora conserva sus campos anteriores");
    }

    section("Consultas que no se envian al probador");
    {
        const int before = prover_requests.load();
        const ui::ApiReply bad = portal.query("C99 1");
        const ui::ApiReply unsupported = portal.query("range student program_id 1 2");
        check(bad.status == 400 && has(bad.body, "\"outcome\":\"invalid\""), "consulta desconocida: 400");
        check(unsupported.status == 422 && has(unsupported.body, "\"outcome\":\"unsupported\""),
              "consulta no soportada: 422");
        check(prover_requests.load() == before, "ninguna de las dos llego al probador");
        check(records.back().outcome == "unsupported", "la bitacora registra el motivo");
    }

    section("Probador con otro estado (RNF08)");
    {
        test::ServiceFixture other(60);
        RunningProver stale(other.context());
        ui::Portal p(fx.state.digest, fx.keys.pk.aso, portal_options(stale.endpoint()));
        check(has(p.status().body, "\"matches\":false"), "/api/status avisa que el estado no coincide");
        const ui::ApiReply r = p.query("C4 6 8");
        check(r.status == 200 && has(r.body, "\"outcome\":\"rejected\"") && !has(r.body, "\"result\"") &&
                  has(r.body, "\"timings\":{") && has(r.body, "\"client_total_seconds\":"),
              "la respuesta se rechaza sin filas aunque existan metricas no autenticadas");
    }

    section("Probador sin clave publica o detenido");
    {
        query::ProverContext no_pk = fx.context();
        no_pk.public_key = nullptr;
        RunningProver partial(no_pk);
        ui::Portal p(fx.state.digest, fx.keys.pk.aso, portal_options(partial.endpoint()));
        const ui::ApiReply c7 = p.query("C7 6 8 20241 20242");
        check(has(c7.body, "\"outcome\":\"no_answer\"") && has(c7.body, "\"status\":500") &&
                  has(c7.body, "\"timings\":null") && !has(c7.body, "\"result\""),
              "C7 sin clave: no inventa metricas del servidor ni devuelve filas");
        check(has(p.query("C2 1").body, "\"outcome\":\"verified\""), "C2 se sigue verificando");
        partial.stop();
        const ui::ApiReply down = p.query("C2 1");
        check(has(down.body, "\"outcome\":\"no_answer\"") && has(down.body, "\"status\":0") &&
                  has(down.body, "\"timings\":null") && has(down.body, "\"network_overhead_seconds\":null"),
              "probador detenido: sin respuesta ni metricas remotas");
        check(has(p.status().body, "\"reachable\":false"), "/api/status indica que no responde");
    }

    section("Servidor HTTP de la interfaz");
    {
        const fs::path root = make_web_dir();
        check_throws<std::invalid_argument>(
            [&] {
                ui::UiServerOptions o;
                o.web_dir = root / "no_existe";
                ui::UiServer s(portal, o);
            },
            "sin index.html no arranca");
        RunningUi http(portal, root / "web");
        httplib::Client client("127.0.0.1", http.port());
        client.set_read_timeout(60, 0);

        auto page = client.Get("/");
        check(page && page->status == 200 && has(page->body, "pagina-de-prueba") &&
                  page->get_header_value("Content-Type") == "text/html; charset=utf-8",
              "GET / entrega index.html en UTF-8");
        check(page && has(page->get_header_value("Content-Security-Policy"), "default-src 'self'") &&
                  page->get_header_value("X-Content-Type-Options") == "nosniff",
              "cabeceras de seguridad en la pagina");
        auto js = client.Get("/app.js");
        check(js && js->status == 200 && has(js->get_header_value("Content-Type"), "text/javascript"), "GET /app.js");
        auto outside = client.Get("/../secret.txt");
        check(outside && outside->status != 200 && !has(outside->body, "no-debe-salir"),
              "no se leen archivos fuera de la carpeta de la pagina");

        auto status = client.Get("/api/status");
        check(status && status->status == 200 && has(status->body, "\"matches\":true") &&
                  status->get_header_value("Cache-Control") == "no-store",
              "GET /api/status (JSON, sin cache)");

        auto no_header = client.Post("/api/query", "C2 1", "text/plain");
        check(no_header && no_header->status == 403, "POST sin X-TT-UI: 403");
        auto no_header_direct = client.Post("/api/direct", "C2 1", "text/plain");
        check(no_header_direct && no_header_direct->status == 403,
              "SQL directo tambien exige X-TT-UI");

        const httplib::Headers ui_header = {{ui::kUiHeader, "1"}};
        auto direct_disabled = client.Post("/api/direct", ui_header, "C2 1", "text/plain");
        check(direct_disabled && direct_disabled->status == 403,
              "SQL directo no se ejecuta sin habilitar el modo experimental");
        auto query = client.Post("/api/query", ui_header, "C2 1", "text/plain");
        check(query && query->status == 200 && has(query->body, "\"outcome\":\"verified\"") &&
                  has(query->get_header_value("Content-Type"), "application/json"),
              "POST /api/query verificada");
        auto preview = client.Post("/api/preview", ui_header, "C4 8 6", "text/plain");
        check(preview && preview->status == 400 && has(preview->body, "\"outcome\":\"invalid\""),
              "POST /api/preview mal escrita: 400");
        auto large = client.Post("/api/query", ui_header, std::string(net::kMaxQueryBytes + 1, 'a'), "text/plain");
        check(large && large->status == 413, "consulta demasiado larga: 413");
        auto unknown = client.Get("/api/nada");
        check(unknown && unknown->status == 404 && has(unknown->body, "\"outcome\":\"error\""),
              "ruta desconocida de la API: 404 en JSON");
        fs::remove_all(root);
    }

    return test::finish();
}

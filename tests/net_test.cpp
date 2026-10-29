// net_test.cpp
//
// Pruebas de la comunicacion por red (biblioteca tt_net) sin MySQL: el
// servidor del probador se levanta en este mismo proceso, en un puerto
// libre de 127.0.0.1, con la base pequena en memoria.
//   - Formato de las direcciones y del estado (/v1/health).
//   - Las 14 formas del catalogo viajan por la red y se verifican.
//   - Errores del servidor (400, 413, 422, 500) y de la red (sin servidor,
//     respuesta demasiado grande).
//   - Varias consultas al mismo tiempo.
//   - Un probador con otro estado: sus respuestas se rechazan (RNF08).

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "service_fixture.hpp"
#include "test_support.hpp"
#include "tt/net/prover_client.hpp"
#include "tt/net/prover_server.hpp"
#include "tt/net/protocol.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

namespace {

// Un servidor del probador ejecutandose en su propio hilo.
class RunningServer {
public:
    /*
     * Abre un puerto libre en 127.0.0.1 y empieza a atender en otro hilo.
     */
    explicit RunningServer(query::ProverContext context, net::RequestLogger logger = {}) {
        net::ServerOptions options;
        options.host = "127.0.0.1";
        options.port = 0;
        server_ = std::make_unique<net::ProverServer>(context, options, std::move(logger));
        port_ = server_->bind();
        thread_ = std::thread([this] { server_->run(); });
        server_->wait_until_ready();
    }

    /*
     * Detiene el servidor y espera a que termine su hilo.
     */
    ~RunningServer() { stop(); }

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

/*
 * Texto de una consulta a partir de sus palabras.
 */
std::string text_of(const std::vector<std::string>& words) { return net::join_words(words); }

/*
 * Codigo HTTP de la ServerError que lanza fn, o 0 si no lanzo esa excepcion.
 */
template <class Fn>
int server_status(Fn&& fn) {
    try {
        fn();
    } catch (const net::ServerError& e) {
        return e.status();
    } catch (...) {
    }
    return 0;
}

}  // namespace

/*
 * Ejecuta las comprobaciones y devuelve 0 si todas pasan.
 */
int main() {
    section("Direcciones, estado y texto");
    {
        const net::Endpoint a = net::parse_endpoint("prover:7070");
        const net::Endpoint b = net::parse_endpoint("127.0.0.1");
        check(a.host == "prover" && a.port == 7070, "host:puerto");
        check(b.host == "127.0.0.1" && b.port == net::kDefaultPort, "solo host usa el puerto por omision");
        for (const char* bad : {"http://prover:7070", ":7070", "prover:0", "prover:70000", "prover:", "pro ver:1",
                                "prover:12a"}) {
            check_throws<net::AddressError>([&] { net::parse_endpoint(bad); }, std::string("rechaza '") + bad + "'");
        }
        net::HealthInfo info;
        info.state_id[0] = 0xab;
        info.state_version = 3;
        info.plan = "queries";
        info.public_key = true;
        const net::HealthInfo back = net::health_from_text(net::health_to_text(info));
        check(back.state_id == info.state_id && back.state_version == 3 && back.plan == "queries" && back.public_key,
              "el estado se escribe y se lee igual");
        check_throws<std::runtime_error>([] { net::health_from_text("<html>hola</html>"); },
                                         "un estado con otro formato se rechaza");
        check(net::safe_text("ab\x01\ncd") == "ab??cd", "safe_text cambia los caracteres de control");
        check(net::safe_text(std::string(500, 'x'), 10) == "xxxxxxxxxx...", "safe_text recorta");
        check(net::split_words("  C4\t6 \n8 ") == std::vector<std::string>{"C4", "6", "8"} &&
                  net::join_words({"C4", "6", "8"}) == "C4 6 8",
              "palabras de la consulta");
    }

    test::ServiceFixture fx;
    std::atomic<int> logged{0};
    RunningServer server(fx.context(), [&](const net::RequestRecord&) { ++logged; });
    const net::Endpoint at = server.endpoint();

    section("Estado del probador por la red");
    {
        const net::HealthInfo info = net::fetch_health(at);
        check(info.state_id == fx.state.digest.id() && info.public_key && info.plan == "queries",
              "/v1/health publica el id de delta y que tiene la clave publica");
    }

    section("Las 14 formas del catalogo por la red");
    for (const test::CatalogCase& c : test::catalog_cases()) {
        const query::ParsedQuery q = query::parse_query(c.words);
        const std::vector<unsigned char> bytes = net::fetch_answer(at, text_of(c.words));
        const query::VerifierOutcome v = query::verify_query(fx.state.digest, fx.keys.pk.aso, q, bytes);
        check(v.accepted && test::matches(v.result, test::oracle(fx.base, c.words)),
              text_of(c.words) + ": recibida, verificada y correcta" + (v.accepted ? "" : " (" + v.reason + ")"));
    }
    {
        const query::ParsedQuery q = query::parse_query({"C4", "6", "8"});
        const std::vector<unsigned char> local = query::answer_query(fx.context(), q).bytes;
        check(net::fetch_answer(at, "C4 6 8") == local, "por la red llegan los mismos bytes que genera el nucleo");
        check(net::fetch_answer(at, "  C4   6\t8\n") == local, "los espacios de mas no cambian la consulta");
    }

    section("Errores que informa el probador");
    check(server_status([&] { net::fetch_answer(at, "C4 8 6"); }) == 400, "rango invertido: 400");
    check(server_status([&] { net::fetch_answer(at, "C99 1"); }) == 400, "consulta desconocida: 400");
    check(server_status([&] { net::fetch_answer(at, ""); }) == 400, "consulta vacia: 400");
    check(server_status([&] { net::fetch_answer(at, "C4 6\x01 8"); }) == 400, "caracteres de control: 400");
    check(server_status([&] { net::fetch_answer(at, std::string(net::kMaxQueryBytes + 1, 'a')); }) == 413,
          "consulta demasiado larga: 413");
    check(server_status([&] { net::fetch_answer(at, "range student program_id 1 2"); }) == 422,
          "consulta sin arbol en delta: 422");
    {
        std::string message;
        try {
            net::fetch_answer(at, "C4 8 6");
        } catch (const net::ServerError& e) {
            message = e.what();
        }
        check(message.find("el rango esta invertido") != std::string::npos && message.find('\n') == std::string::npos,
              "el mensaje del servidor llega saneado y en una sola linea");
    }

    section("Probador sin clave publica");
    {
        query::ProverContext no_pk = fx.context();
        no_pk.public_key = nullptr;
        RunningServer partial(no_pk);
        check(!net::fetch_health(partial.endpoint()).public_key, "/v1/health indica que no tiene la clave publica");
        check(server_status([&] { net::fetch_answer(partial.endpoint(), "C7 6 8 20241 20242"); }) == 500,
              "C7 se responde con 500");
        const query::ParsedQuery q = query::parse_query({"C2", "1"});
        check(query::verify_query(fx.state.digest, fx.keys.pk.aso, q, net::fetch_answer(partial.endpoint(), "C2 1")).accepted,
              "C2 se sigue respondiendo");
    }

    section("Limites del cliente");
    {
        net::ClientOptions small;
        small.max_response_bytes = 1000;
        check_throws<net::NetworkError>([&] { net::fetch_answer(at, "C8", small); },
                                        "una respuesta mayor que el limite se corta (NetworkError)");
    }

    section("Varias consultas al mismo tiempo");
    {
        std::atomic<int> accepted{0};
        std::vector<std::thread> clients;
        const std::vector<std::vector<std::string>> queries = {
            {"C4", "6", "8"}, {"C7", "6", "8", "20241", "20242"}, {"C10", "1"}, {"C8"}, {"C2", "3"}, {"C12", "1", "1"}};
        for (const auto& words : queries) {
            clients.emplace_back([&, words] {
                const query::ParsedQuery q = query::parse_query(words);
                const auto bytes = net::fetch_answer(at, text_of(words));
                if (query::verify_query(fx.state.digest, fx.keys.pk.aso, q, bytes).accepted) ++accepted;
            });
        }
        for (std::thread& t : clients) t.join();
        check(accepted.load() == static_cast<int>(queries.size()), "seis clientes a la vez: todas verificadas");
    }

    section("Probador con otro estado (RNF08)");
    {
        test::ServiceFixture other(60);
        RunningServer stale(other.context());
        check(net::fetch_health(stale.endpoint()).state_id != fx.state.digest.id(), "/v1/health muestra otro id");
        const query::ParsedQuery q = query::parse_query({"C4", "6", "8"});
        const query::VerifierOutcome v =
            query::verify_query(fx.state.digest, fx.keys.pk.aso, q, net::fetch_answer(stale.endpoint(), "C4 6 8"));
        check(!v.accepted, "su respuesta se rechaza con el delta del cliente");
    }

    section("Sin servidor");
    check(logged.load() > 0, "el servidor registro las peticiones atendidas");
    server.stop();
    check_throws<net::NetworkError>([&] { net::fetch_answer(at, "C4 6 8"); },
                                    "con el servidor detenido: NetworkError");
    check_throws<net::NetworkError>([&] { net::fetch_health(at); }, "/v1/health tambien: NetworkError");

    return test::finish();
}

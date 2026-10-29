// ping_command.cpp - comando "tt_verifier ping"
//
// Comprueba la conexion con el probador y compara el estado con el que
// responde (id de delta) con el delta que tiene el cliente. Es una ayuda de
// diagnostico: si los ids no coinciden, el verificador rechazara todas las
// respuestas de ese probador (RNF08), y conviene saberlo antes de consultar.
// La seguridad no depende de este comando, porque cada respuesta se
// verifica por completo.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "client_common.hpp"
#include "commands.hpp"
#include "tt/cli/cli.hpp"
#include "tt/net/prover_client.hpp"
#include "tt/net/protocol.hpp"
#include "tt/scheme/digest.hpp"

namespace tt::verifier {

namespace {

namespace fs = std::filesystem;

/*
 * Opciones del comando ping.
 */
struct PingOptions {
    net::Endpoint server;
    fs::path delta = "setup/delta.txt";
};

/*
 * Lee las opciones del comando; la direccion por omision es la misma que en
 * query.
 *
 * Errores:
 *   Lanza cli::UsageError si una opcion no es valida.
 */
PingOptions parse_ping_options(const std::vector<std::string>& args) {
    PingOptions opt;
    const char* env = std::getenv("TT_PROVER_ADDR");
    std::string server = (env != nullptr && *env != '\0') ? env : "127.0.0.1:" + std::to_string(net::kDefaultPort);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--server") {
            server = cli::option_value(args, i);
        } else if (arg == "--delta") {
            opt.delta = cli::option_value(args, i);
        } else {
            throw cli::UsageError("opcion desconocida: " + arg);
        }
    }
    try {
        opt.server = net::parse_endpoint(server);
    } catch (const net::AddressError& e) {
        throw cli::UsageError(e.what());
    }
    return opt;
}

}  // namespace

/*
 * Implementacion: el texto fijo del comando.
 */
std::string ping_usage() {
    return "  tt_verifier ping [--server <host:puerto>] [--delta <archivo>]\n"
           "      Comprueba la conexion con el probador y compara su estado con el delta\n"
           "      del cliente. Codigo de salida: 0 conectado y con el mismo estado, 1 el\n"
           "      estado no coincide (sus respuestas se rechazarian), 4 sin conexion.\n";
}

/*
 * Implementacion: pide /v1/health y, si hay delta local, compara los ids.
 */
int run_ping(const std::vector<std::string>& args) {
    const PingOptions opt = parse_ping_options(args);
    net::ClientOptions client;
    client.read_timeout_seconds = 10;
    net::HealthInfo info;
    try {
        info = net::fetch_health(opt.server, client);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[verifier] sin respuesta: %s\n", e.what());
        return kExitNoAnswer;
    }
    std::printf("[verifier] probador en %s: estado version %llu, id %s (plan %s); clave publica %s\n",
                opt.server.text().c_str(), static_cast<unsigned long long>(info.state_version),
                short_id(info.state_id).c_str(), net::safe_text(info.plan, 40).c_str(),
                info.public_key ? "cargada" : "NO cargada (C7 a C14 fallaran)");
    if (!fs::exists(opt.delta)) {
        std::printf("[verifier] no hay delta local en %s para comparar\n", opt.delta.string().c_str());
        return 0;
    }
    const scheme::DatabaseDigest digest = scheme::load_digest(opt.delta);
    if (digest.id() != info.state_id) {
        std::printf("[verifier] el estado del probador NO coincide con tu delta (%s, id %s): sus respuestas\n"
                    "           se rechazarian. Pide al propietario el delta vigente o revisa el probador.\n",
                    opt.delta.string().c_str(), short_id(digest.id()).c_str());
        return 1;
    }
    std::printf("[verifier] el estado del probador coincide con tu delta (%s)\n", opt.delta.string().c_str());
    return 0;
}

}  // namespace tt::verifier

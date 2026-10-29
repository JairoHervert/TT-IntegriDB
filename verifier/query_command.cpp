// query_command.cpp - comando "tt_verifier query"
//
// Flujo completo del bloque Cliente por la red: envia la consulta al
// probador, recibe la respuesta (R, pi) y la verifica con delta y la clave
// publica antes de mostrar el resultado. El verificador nunca ve D, D~ ni
// la clave secreta; solo los bytes que envia el probador, que se revisan
// como no confiables.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "client_common.hpp"
#include "commands.hpp"
#include "tt/cli/cli.hpp"
#include "tt/io/file.hpp"
#include "tt/net/prover_client.hpp"
#include "tt/net/protocol.hpp"

namespace tt::verifier {

namespace {

namespace fs = std::filesystem;
using cli::Clock;
using cli::seconds_since;

/*
 * Opciones del comando query: las comunes mas la direccion del probador,
 * el tiempo de espera y, si se pide, el archivo donde guardar la respuesta.
 */
struct QueryOptions {
    CommonOptions common;
    std::vector<std::string> words;   // la consulta tal como se escribio
    net::Endpoint server;
    int timeout_seconds = 600;
    fs::path save;                    // vacio: no se guarda
};

/*
 * Lee el tiempo de espera en segundos (entero entre 1 y 86400).
 *
 * Errores:
 *   Lanza cli::UsageError si no es valido.
 */
int parse_timeout(const std::string& text) {
    if (text.empty() || text.size() > 5 || text.find_first_not_of("0123456789") != std::string::npos ||
        std::stoi(text) < 1 || std::stoi(text) > 86400) {
        throw cli::UsageError("--timeout espera segundos entre 1 y 86400, se recibio '" + text + "'");
    }
    return std::stoi(text);
}

/*
 * Lee las opciones del comando. La direccion por omision sale de la
 * variable TT_PROVER_ADDR o, si no existe, es 127.0.0.1 con el puerto por
 * omision.
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta, la direccion o una opcion no son
 *   validas.
 */
QueryOptions parse_query_options(const std::vector<std::string>& args) {
    QueryOptions opt;
    const char* env = std::getenv("TT_PROVER_ADDR");
    std::string server = (env != nullptr && *env != '\0') ? env : "127.0.0.1:" + std::to_string(net::kDefaultPort);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (parse_common_option(args, i, opt.common)) continue;
        if (arg == "--server") {
            server = cli::option_value(args, i);
        } else if (arg == "--timeout") {
            opt.timeout_seconds = parse_timeout(cli::option_value(args, i));
        } else if (arg == "--save") {
            opt.save = cli::option_value(args, i);
        } else if (arg.rfind("--", 0) == 0) {
            throw cli::UsageError("opcion desconocida: " + arg);
        } else {
            opt.words.push_back(arg);
        }
    }
    opt.common.query = parse_query_words(opt.words);
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
std::string query_usage() {
    return std::string(
               "  tt_verifier query <consulta> [--server <host:puerto>] [--keys <carpeta>]\n"
               "                    [--delta <archivo>] [--show <n>] [--aso] [--timeout <s>]\n"
               "                    [--save <archivo>]\n"
               "      Envia la consulta al probador por la red, recibe la respuesta (R, pi) y\n"
               "      la verifica con delta y la clave publica antes de mostrar el resultado.\n"
               "      <consulta> es la misma que acepta 'verify'.\n"
               "      --server <host:puerto>  probador (por defecto: TT_PROVER_ADDR o\n"
               "                              127.0.0.1:") +
           std::to_string(net::kDefaultPort) +
           ").\n"
           "      --keys, --delta, --show, --aso  como en 'verify'.\n"
           "      --timeout <s>           espera maxima por la respuesta (por defecto: 600).\n"
           "      --save <archivo>        guarda tambien la respuesta recibida, para volver a\n"
           "                              verificarla con 'verify'.\n"
           "      Codigo de salida: 0 verificada, 3 rechazada, 1 no se pudo verificar,\n"
           "      4 el probador no entrego respuesta (red o error del servidor).\n";
}

/*
 * Implementacion: primero se prepara todo lo local (delta, arboles y clave
 * publica), para no pedir al probador una consulta que de todos modos no se
 * podria verificar; despues se pide la respuesta y se verifica.
 */
int run_query(const std::vector<std::string>& args) {
    const QueryOptions opt = parse_query_options(args);
    ClientState state;
    if (const int code = prepare_client(opt.common, state); code != 0) return code;

    net::ClientOptions client;
    client.read_timeout_seconds = opt.timeout_seconds;
    std::printf("[verifier] enviando la consulta al probador en %s\n", opt.server.text().c_str());
    const auto start = Clock::now();
    std::vector<unsigned char> bytes;
    try {
        bytes = net::fetch_answer(opt.server, net::join_words(opt.words), client);
    } catch (const net::ServerError& e) {
        std::fprintf(stderr, "[verifier] el probador no entrego una respuesta: %s\n", e.what());
        return kExitNoAnswer;
    } catch (const net::NetworkError& e) {
        std::fprintf(stderr, "[verifier] sin respuesta: %s\n", e.what());
        return kExitNoAnswer;
    }
    std::printf("[verifier] respuesta del probador en %s: %s recibidos en %.2f s\n", opt.server.text().c_str(),
                cli::size_text(bytes.size()).c_str(), seconds_since(start));
    if (!opt.save.empty()) {
        if (opt.save.has_parent_path()) fs::create_directories(opt.save.parent_path());
        io::write_file_atomic(opt.save, bytes);
        std::printf("[verifier] respuesta guardada en %s\n", opt.save.string().c_str());
    }
    return verify_and_report(opt.common, state, bytes);
}

}  // namespace tt::verifier

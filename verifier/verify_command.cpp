// verify_command.cpp - comando "tt_verifier verify"
//
// Verifica una respuesta guardada en un archivo (por ejemplo, la que
// escribio 'tt_prover answer' o la que se guardo con 'tt_verifier query
// --save'). Las reglas son las de query::verify_query, las mismas que usa el
// comando query con las respuestas que llegan por la red.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "client_common.hpp"
#include "commands.hpp"
#include "tt/cli/cli.hpp"
#include "tt/io/file.hpp"

namespace tt::verifier {

namespace {

namespace fs = std::filesystem;

/*
 * Opciones del comando verify: las comunes mas el archivo de la respuesta.
 */
struct VerifyOptions {
    CommonOptions common;
    fs::path response = "respuesta.bin";
};

/*
 * Lee las opciones: las palabras que no empiezan con "--" forman la
 * consulta; el resto son opciones con su valor.
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta no es valida o si una opcion es
 *   desconocida o le falta su valor.
 */
VerifyOptions parse_verify_options(const std::vector<std::string>& args) {
    VerifyOptions opt;
    std::vector<std::string> words;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (parse_common_option(args, i, opt.common)) continue;
        if (arg == "--response") {
            opt.response = cli::option_value(args, i);
        } else if (arg.rfind("--", 0) == 0) {
            throw cli::UsageError("opcion desconocida: " + arg);
        } else {
            words.push_back(arg);
        }
    }
    opt.common.query = parse_query_words(words);
    return opt;
}

}  // namespace

/*
 * Implementacion: el texto fijo del comando mas las formas de consulta del
 * nucleo.
 */
std::string verify_usage() {
    return std::string(
               "  tt_verifier verify <consulta> [--response <archivo>] [--keys <carpeta>]\n"
               "                     [--delta <archivo>] [--show <n>] [--aso]\n"
               "      Verify de una respuesta guardada en un archivo: la revisa contra el\n"
               "      digest delta y la clave publica, y muestra el resultado verificado.\n"
               "      <consulta> es la misma que se le pidio al probador:\n") +
           query::kQueryForms +
           "      --response <archivo>  respuesta del probador (por defecto: respuesta.bin).\n"
           "      --keys <carpeta>      donde esta public_key.txt (por defecto: keys).\n"
           "      --delta <archivo>     digest delta vigente (por defecto: setup/delta.txt).\n"
           "      --show <n>            filas del resultado que se muestran (por defecto: 10).\n"
           "      --aso                 en C1 a C6, comprueba ademas el acumulador de cada nodo\n"
           "                            de cobertura, como el procedimiento del articulo (con\n"
           "                            los contadores autenticados es redundante y mas lento).\n"
           "      Codigo de salida: 0 verificada, 3 rechazada, 1 no se pudo verificar.\n";
}

/*
 * Implementacion: prepara delta y la clave publica, lee el archivo y
 * delega la decision en verify_and_report.
 */
int run_verify(const std::vector<std::string>& args) {
    const VerifyOptions opt = parse_verify_options(args);
    ClientState state;
    if (const int code = prepare_client(opt.common, state); code != 0) return code;
    if (!fs::exists(opt.response)) {
        std::fprintf(stderr, "[verifier] no existe la respuesta %s\n", opt.response.string().c_str());
        return 1;
    }
    const std::vector<unsigned char> bytes = io::read_file(opt.response);
    std::printf("[verifier] respuesta del probador: %s (%s)\n", opt.response.string().c_str(),
                cli::size_text(bytes.size()).c_str());
    return verify_and_report(opt.common, state, bytes);
}

}  // namespace tt::verifier

// main.cpp - Cliente SQL + modulo verificador (bloque Cliente)
//
// Responsabilidad (arquitectura logica del reporte): enviar la consulta Q al
// probador, recibir (R, pi) y evaluar Verify(delta, Q, R, pi) con la clave
// publica pk. Entrega R al Cliente SQL junto con el veredicto.
//
// Por ahora la respuesta del probador llega como archivo; cuando se conecte
// la red, el mismo contenido llegara como mensaje.
//
// Comandos (cada uno en su archivo):
//   tt_verifier verify   verify_command.cpp

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "commands.hpp"
#include "tt/cli/cli.hpp"

namespace {

/*
 * Imprime la ayuda general en el flujo dado.
 */
void print_usage(std::FILE* out) { std::fprintf(out, "Uso:\n%s", tt::verifier::verify_usage().c_str()); }

}  // namespace

/*
 * Punto de entrada del verificador. Lee el comando, lo ejecuta y traduce
 * los errores a codigos de salida:
 *   0  exito: la respuesta se verifico (o --help)
 *   1  error al ejecutar el comando (archivo faltante, consulta no
 *      soportada, clave publica que no corresponde a delta...)
 *   2  error de uso (se muestra la ayuda)
 *   3  la respuesta del servidor fue rechazada
 */
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // cada linea sale en orden con los mensajes de stderr
    const std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            print_usage(args.empty() ? stderr : stdout);
            return args.empty() ? 2 : 0;
        }
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (args[0] == "verify") return tt::verifier::run_verify(rest);
        throw tt::cli::UsageError("comando desconocido: " + args[0]);
    } catch (const tt::cli::UsageError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "[verifier] %s\n\n", e.what());
        print_usage(stderr);
        return 2;
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "[verifier] error: %s\n", e.what());
        return 1;
    }
}

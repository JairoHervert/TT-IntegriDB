// main.cpp - Modulo probador (bloque Servidor)
//
// Responsabilidad (arquitectura logica del reporte): recibir la consulta Q
// del verificador, obtener del Servidor SQL las filas necesarias y generar
// el par (R, pi) usando D~. Nunca conoce el secreto s ni la clave de Enc.
//
// Por ahora la respuesta se entrega como archivo; cuando se conecte la red,
// el mismo contenido sera el mensaje al verificador.
//
// Comandos (cada uno en su archivo):
//   tt_prover answer   answer_command.cpp

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
void print_usage(std::FILE* out) { std::fprintf(out, "Uso:\n%s", tt::prover::answer_usage().c_str()); }

}  // namespace

/*
 * Punto de entrada del probador. Lee el comando, lo ejecuta y traduce los
 * errores a codigos de salida:
 *   0  exito (o --help)
 *   1  error al ejecutar el comando
 *   2  error de uso (se muestra la ayuda)
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
        if (args[0] == "answer") return tt::prover::run_answer(rest);
        throw tt::cli::UsageError("comando desconocido: " + args[0]);
    } catch (const tt::cli::UsageError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "[prover] %s\n\n", e.what());
        print_usage(stderr);
        return 2;
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "[prover] error: %s\n", e.what());
        return 1;
    }
}

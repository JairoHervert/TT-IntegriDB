// main.cpp - Propietario de datos
//
// Responsabilidad (arquitectura logica del reporte): ejecutar Init para
// generar (sk_D, pk) y Setup para construir la base autenticada D~ y el
// digest delta. Entrega pk y delta al verificador, y D~ al servidor.
// Es el unico programa que conoce el secreto s.
//
// Comandos (cada uno en su archivo):
//   tt_owner init     init_command.cpp
//   tt_owner setup    setup_command.cpp
//   tt_owner catalog  catalog_command.cpp

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "cli.hpp"
#include "commands.hpp"

namespace {

/*
 * Imprime la ayuda general (la de todos los comandos) en el flujo dado.
 */
void print_usage(std::FILE* out) {
    std::fprintf(out, "Uso:\n%s\n%s\n%s", tt::owner::kInitUsage, tt::owner::kSetupUsage, tt::owner::kCatalogUsage);
}

}  // namespace

/*
 * Punto de entrada del propietario de datos. Lee el comando, lo ejecuta y
 * traduce los errores a codigos de salida:
 *   0  exito (o --help)
 *   1  error al ejecutar el comando
 *   2  error de uso (se muestra la ayuda)
 */
int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            print_usage(args.empty() ? stderr : stdout);
            return args.empty() ? 2 : 0;
        }
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (args[0] == "init") return tt::owner::run_init(rest);
        if (args[0] == "setup") return tt::owner::run_setup(rest);
        if (args[0] == "catalog") return tt::owner::run_catalog(rest);
        throw tt::owner::UsageError("comando desconocido: " + args[0]);
    } catch (const tt::owner::UsageError& e) {
        std::fflush(stdout);  // que lo ya impreso salga antes que el error
        std::fprintf(stderr, "[owner] %s\n\n", e.what());
        print_usage(stderr);
        return 2;
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "[owner] error: %s\n", e.what());
        return 1;
    }
}

// main.cpp - Propietario de datos
//
// Responsabilidad (arquitectura logica del reporte): ejecutar Init para
// generar (sk_D, pk) y Setup para construir la base autenticada D~ y el
// digest delta. Entrega pk y delta al verificador, y D~ al servidor.
// Es el unico programa que conoce el secreto s.
//
// Comandos:
//   tt_owner init --q <n> [--out <carpeta>] [--check] [--force]
//
// Setup se agregara como otro comando cuando exista el AIT.

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "tt/aso/keys.hpp"
#include "tt/scheme/key_io.hpp"
#include "tt/scheme/keys.hpp"

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

const char* const kUsage =
    "Uso:\n"
    "  tt_owner init --q <n> [--out <carpeta>] [--check] [--force]\n"
    "\n"
    "init  Algoritmo Init del esquema: genera la clave secreta sk_D del\n"
    "      propietario y la clave publica pk.\n"
    "      --q <n>          cota superior del tamano de cualquier conjunto\n"
    "                       autenticado: al menos el numero de filas de la\n"
    "                       tabla mas grande (academic_record).\n"
    "      --out <carpeta>  donde se escriben public_key.txt y secret_key.txt\n"
    "                       (por defecto: keys).\n"
    "      --check          despues de generar, comprueba la estructura de pk\n"
    "                       (tarda aproximadamente lo mismo que init).\n"
    "      --force          reemplaza claves existentes en la carpeta.\n";

struct InitOptions {
    std::size_t q = 0;
    fs::path out = "keys";
    bool check = false;
    bool force = false;
};

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::size_t parse_q(const std::string& text) {
    if (text.empty() || text.size() > 9 || text.find_first_not_of("0123456789") != std::string::npos) {
        throw UsageError("--q espera un entero positivo, se recibio '" + text + "'");
    }
    const std::size_t q = static_cast<std::size_t>(std::stoul(text));
    if (q == 0 || q > tt::aso::kMaxQ) {
        throw UsageError("--q debe estar entre 1 y " + std::to_string(tt::aso::kMaxQ));
    }
    return q;
}

InitOptions parse_init_options(const std::vector<std::string>& args) {
    InitOptions opt;
    bool has_q = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size()) throw UsageError(arg + " necesita un valor");
            return args[++i];
        };
        if (arg == "--q") {
            opt.q = parse_q(value());
            has_q = true;
        } else if (arg == "--out") {
            opt.out = value();
        } else if (arg == "--check") {
            opt.check = true;
        } else if (arg == "--force") {
            opt.force = true;
        } else {
            throw UsageError("opcion desconocida: " + arg);
        }
    }
    if (!has_q) throw UsageError("falta --q");
    return opt;
}

double file_size_mb(const fs::path& path) {
    return static_cast<double>(fs::file_size(path)) / (1024.0 * 1024.0);
}

int run_init(const InitOptions& opt) {
    const fs::path pk_path = opt.out / "public_key.txt";
    const fs::path sk_path = opt.out / "secret_key.txt";

    if (!opt.force && (fs::exists(pk_path) || fs::exists(sk_path))) {
        std::fprintf(stderr,
                     "[owner] ya hay claves en %s.\n"
                     "        Generar otras invalidaria todo lo que se haya autenticado con las\n"
                     "        actuales. Si de verdad quieres reemplazarlas, agrega --force.\n",
                     opt.out.string().c_str());
        return 1;
    }
    fs::create_directories(opt.out);

    std::printf("[owner] Init: generando claves con q = %zu\n", opt.q);
    const auto t_init = Clock::now();
    const tt::scheme::KeyPair keys = tt::scheme::init(opt.q);
    std::printf("[owner] Init terminado en %.2f s\n", seconds_since(t_init));

    if (opt.check) {
        const auto t_check = Clock::now();
        const bool ok = tt::aso::is_well_formed(keys.pk.aso);
        std::printf("[owner] estructura de pk: %s (%.2f s)\n", ok ? "correcta" : "INCORRECTA",
                    seconds_since(t_check));
        if (!ok) return 1;
    }

    const auto t_save = Clock::now();
    tt::scheme::save_secret_key(sk_path, keys.sk,
                                opt.force ? tt::scheme::Overwrite::kYes : tt::scheme::Overwrite::kNo);
    tt::scheme::save_public_key(pk_path, keys.pk);
    std::printf("[owner] claves guardadas en %.2f s\n", seconds_since(t_save));

    std::printf("[owner] clave publica pk:   %s (%.1f MB) -> para el probador y el verificador\n",
                pk_path.string().c_str(), file_size_mb(pk_path));
    std::printf("[owner] clave secreta sk_D: %s (permisos 600) -> no sale del propietario\n",
                sk_path.string().c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            std::fputs(kUsage, args.empty() ? stderr : stdout);
            return args.empty() ? 2 : 0;
        }
        if (args[0] == "init") {
            return run_init(parse_init_options({args.begin() + 1, args.end()}));
        }
        throw UsageError("comando desconocido: " + args[0]);
    } catch (const UsageError& e) {
        std::fprintf(stderr, "[owner] %s\n\n%s", e.what(), kUsage);
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[owner] error: %s\n", e.what());
        return 1;
    }
}

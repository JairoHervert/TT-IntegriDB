// init_command.cpp - comando "tt_owner init"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cli.hpp"
#include "commands.hpp"
#include "tt/aso/keys.hpp"
#include "tt/scheme/key_io.hpp"
#include "tt/scheme/keys.hpp"

namespace tt::owner {

const char* const kInitUsage =
    "  tt_owner init --q <n> [--out <carpeta>] [--check] [--force]\n"
    "      Algoritmo Init del esquema: genera la clave secreta sk_D del\n"
    "      propietario y la clave publica pk.\n"
    "      --q <n>          cota superior del tamano de cualquier conjunto\n"
    "                       autenticado: al menos el numero de filas de la\n"
    "                       tabla mas grande (academic_record).\n"
    "      --out <carpeta>  donde se escriben public_key.txt y secret_key.txt\n"
    "                       (por defecto: keys).\n"
    "      --check          despues de generar, comprueba la estructura de pk\n"
    "                       (tarda aproximadamente lo mismo que init).\n"
    "      --force          reemplaza claves existentes en la carpeta.\n";

namespace {

namespace fs = std::filesystem;

/*
 * Opciones del comando init, ya leidas de la linea de comandos.
 *
 * Campos:
 *   q      cota superior del tamano de cualquier conjunto autenticado
 *   out    carpeta donde se escriben public_key.txt y secret_key.txt
 *   check  si es true, se comprueba la estructura de pk despues de generarla
 *   force  si es true, se reemplazan claves que ya existan en `out`
 */
struct InitOptions {
    std::size_t q = 0;
    fs::path out = "keys";
    bool check = false;
    bool force = false;
};

/*
 * Convierte el texto de --q en numero y revisa que este en el rango
 * permitido. Se revisa el texto a mano (solo digitos, a lo mas 9) porque
 * std::stoul acepta cosas como "12abc" o "-3" sin quejarse.
 *
 * Parametros:
 *   text  valor escrito despues de --q
 *
 * Devuelve:
 *   q, entre 1 y aso::kMaxQ.
 *
 * Errores:
 *   Lanza UsageError si el texto no es un entero positivo o esta fuera de
 *   rango.
 */
std::size_t parse_q(const std::string& text) {
    if (text.empty() || text.size() > 9 || text.find_first_not_of("0123456789") != std::string::npos) {
        throw UsageError("--q espera un entero positivo, se recibio '" + text + "'");
    }
    const std::size_t q = static_cast<std::size_t>(std::stoul(text));
    if (q == 0 || q > aso::kMaxQ) {
        throw UsageError("--q debe estar entre 1 y " + std::to_string(aso::kMaxQ));
    }
    return q;
}

/*
 * Lee las opciones del comando init.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "init"
 *
 * Devuelve:
 *   Las opciones leidas; las que no se escribieron quedan con su valor por
 *   defecto.
 *
 * Errores:
 *   Lanza UsageError si falta --q, si una opcion es desconocida o si una
 *   opcion que necesita valor viene al final sin el.
 */
InitOptions parse_init_options(const std::vector<std::string>& args) {
    InitOptions opt;
    bool has_q = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--q") {
            opt.q = parse_q(option_value(args, i));
            has_q = true;
        } else if (arg == "--out") {
            opt.out = option_value(args, i);
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

}  // namespace

/*
 * Implementacion: antes de generar nada revisa que no haya claves previas
 * en la carpeta (reemplazarlas invalidaria todo lo autenticado con ellas,
 * por eso solo se permite con --force). Despues ejecuta Init, opcionalmente
 * comprueba la estructura de pk y guarda ambas claves, informando el tiempo
 * de cada etapa.
 */
int run_init(const std::vector<std::string>& args) {
    const InitOptions opt = parse_init_options(args);
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
    const scheme::KeyPair keys = scheme::init(opt.q);
    std::printf("[owner] Init terminado en %.2f s\n", seconds_since(t_init));

    if (opt.check) {
        const auto t_check = Clock::now();
        const bool ok = aso::is_well_formed(keys.pk.aso);
        std::printf("[owner] estructura de pk: %s (%.2f s)\n", ok ? "correcta" : "INCORRECTA",
                    seconds_since(t_check));
        if (!ok) return 1;
    }

    const auto t_save = Clock::now();
    scheme::save_secret_key(sk_path, keys.sk, opt.force ? scheme::Overwrite::kYes : scheme::Overwrite::kNo);
    scheme::save_public_key(pk_path, keys.pk);
    std::printf("[owner] claves guardadas en %.2f s\n", seconds_since(t_save));

    std::printf("[owner] clave publica pk:   %s (%s) -> para el probador y el verificador\n",
                pk_path.string().c_str(), file_size_text(pk_path).c_str());
    std::printf("[owner] clave secreta sk_D: %s (permisos 600) -> no sale del propietario\n",
                sk_path.string().c_str());
    return 0;
}

}  // namespace tt::owner

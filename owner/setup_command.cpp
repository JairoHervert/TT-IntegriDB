// setup_command.cpp - comando "tt_owner setup"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli.hpp"
#include "commands.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/mysql/connection.hpp"
#include "tt/mysql/database_source.hpp"
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"
#include "tt/scheme/key_io.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace tt::owner {

const char* const kSetupUsage =
    "  tt_owner setup [--keys <carpeta>] [--trees queries|full] [--out <carpeta>] [--force]\n"
    "      Algoritmo Setup del esquema: lee la base de MySQL y construye la\n"
    "      base autenticada D~ (para el servidor) y el digest delta (publico).\n"
    "      --keys <carpeta>  donde estan public_key.txt y secret_key.txt\n"
    "                        (por defecto: keys).\n"
    "      --trees <plan>    que arboles AIT construir:\n"
    "                          queries  solo los que usa el catalogo de consultas\n"
    "                                   del caso de estudio (por defecto);\n"
    "                          full     todos los pares de columnas enteras de\n"
    "                                   cada tabla (m^2), como el algoritmo Setup\n"
    "                                   del reporte.\n"
    "      --out <carpeta>   donde se escriben delta.txt y authenticated_db.bin\n"
    "                        (por defecto: setup).\n"
    "      --force           reemplaza un delta y un D~ existentes.\n"
    "      La conexion se toma de TT_DB_HOST, TT_DB_PORT, TT_DB_USER, TT_DB_NAME\n"
    "      y TT_DB_PASS (obligatoria).\n";

namespace {

namespace fs = std::filesystem;

/*
 * Opciones del comando setup, ya leidas de la linea de comandos.
 *
 * Campos:
 *   keys    carpeta con las claves que genero init
 *   preset  plan de arboles (queries o full)
 *   out     carpeta donde se escriben delta y D~
 *   force   si es true, se reemplazan archivos existentes en `out`
 */
struct SetupOptions {
    fs::path keys = "keys";
    scheme::Preset preset = scheme::Preset::kQueries;
    fs::path out = "setup";
    bool force = false;
};

/*
 * Lee las opciones del comando setup.
 *
 * Errores:
 *   Lanza UsageError si una opcion es desconocida, le falta su valor o el
 *   plan no es 'queries' ni 'full'.
 */
SetupOptions parse_setup_options(const std::vector<std::string>& args) {
    SetupOptions opt;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--keys") {
            opt.keys = option_value(args, i);
        } else if (arg == "--trees") {
            const std::string& name = option_value(args, i);
            const std::optional<scheme::Preset> preset = scheme::parse_preset(name);
            if (!preset) throw UsageError("--trees espera 'queries' o 'full', se recibio '" + name + "'");
            opt.preset = *preset;
        } else if (arg == "--out") {
            opt.out = option_value(args, i);
        } else if (arg == "--force") {
            opt.force = true;
        } else {
            throw UsageError("opcion desconocida: " + arg);
        }
    }
    return opt;
}

/*
 * Junta los ids de las consultas que usan un arbol ("C4 C7 C11") o "-" si
 * ninguna lo usa.
 */
std::string used_by_text(const std::vector<std::string>& ids) {
    if (ids.empty()) return "-";
    std::string text;
    for (const std::string& id : ids) text += (text.empty() ? "" : " ") + id;
    return text;
}

/*
 * Muestra el avance de Setup: una linea por tabla (arbol de Merkle de sus
 * filas) y una por AIT, con su tamano, su tiempo y las consultas del
 * catalogo que lo usan.
 */
class ProgressPrinter {
public:
    /*
     * Guarda el plan para poder mostrar que consultas usan cada arbol.
     */
    explicit ProgressPrinter(const std::vector<scheme::PlannedTree>& plan) : plan_(plan) {}

    /*
     * Recibe un aviso de Setup y lo imprime, acumulando los tiempos.
     */
    void operator()(const scheme::SetupProgress& p) {
        if (p.stage == scheme::SetupProgress::Stage::kRows) {
            if (!rows_header_) {
                std::printf("[owner] filas de cada tabla (hash por fila + arbol de Merkle):\n");
                rows_header_ = true;
            }
            std::printf("          %-18s %8llu filas   %7.2f s\n", p.table.c_str(),
                        static_cast<unsigned long long>(p.items), p.seconds);
            rows_seconds += p.seconds;
            return;
        }
        if (!trees_header_) {
            std::printf("[owner] arboles AIT (clave -> valor), con las consultas del catalogo que los usan:\n");
            trees_header_ = true;
        }
        std::printf("        %3zu/%-3zu %-52s %7llu hojas %8llu nodos %8.2f s   %s\n", p.position, p.total,
                    p.tree->label().c_str(), static_cast<unsigned long long>(p.items),
                    static_cast<unsigned long long>(p.nodes), p.seconds, used_by(*p.tree).c_str());
        std::fflush(stdout);
        trees_seconds += p.seconds;
    }

    double rows_seconds = 0;
    double trees_seconds = 0;

private:
    /*
     * Consultas del catalogo que usan el arbol, como texto.
     */
    std::string used_by(const scheme::TreeSpec& spec) const {
        for (const scheme::PlannedTree& t : plan_) {
            if (t.spec == spec) return used_by_text(t.used_by);
        }
        return "-";
    }

    const std::vector<scheme::PlannedTree>& plan_;
    bool rows_header_ = false;
    bool trees_header_ = false;
};

}  // namespace

/*
 * Implementacion: primero revisa lo que puede fallar sin trabajar (no
 * sobrescribir un estado existente, que existan las claves y que esten las
 * variables de conexion); despues carga las claves, lee la base, arma el
 * plan, ejecuta Setup mostrando los tiempos y guarda delta y D~.
 */
int run_setup(const std::vector<std::string>& args) {
    const SetupOptions opt = parse_setup_options(args);
    const fs::path delta_path = opt.out / "delta.txt";
    const fs::path adb_path = opt.out / "authenticated_db.bin";
    if (!opt.force && (fs::exists(delta_path) || fs::exists(adb_path))) {
        std::fprintf(stderr,
                     "[owner] ya hay un estado autenticado en %s.\n"
                     "        Reemplazarlo hace que el verificador rechace todo lo que se responda con\n"
                     "        el delta anterior. Si de verdad quieres reemplazarlo, agrega --force.\n",
                     opt.out.string().c_str());
        return 1;
    }

    const fs::path sk_path = opt.keys / "secret_key.txt";
    const fs::path pk_path = opt.keys / "public_key.txt";
    if (!fs::exists(sk_path) || !fs::exists(pk_path)) {
        std::fprintf(stderr, "[owner] no hay claves en %s: ejecuta primero 'tt_owner init --q N --out %s'.\n",
                     opt.keys.string().c_str(), opt.keys.string().c_str());
        return 1;
    }
    mysql::DatabaseSource source(mysql::ConnectionConfig::from_environment());

    const auto t_total = Clock::now();
    std::printf("[owner] Setup con el plan '%s'\n", scheme::preset_name(opt.preset));

    const auto t_keys = Clock::now();
    const scheme::SecretKey sk = scheme::load_secret_key(sk_path);
    const scheme::PublicKey pk = scheme::load_public_key(pk_path);
    std::printf("[owner] claves de %s cargadas (q = %zu) en %.2f s\n", opt.keys.string().c_str(), pk.aso.q(),
                seconds_since(t_keys));

    std::printf("[owner] leyendo %s\n", source.description().c_str());
    const auto t_read = Clock::now();
    const db::Database database = source.read();
    std::size_t rows = 0;
    std::vector<db::TableSchema> schemas;
    for (const db::Table& t : database.tables) {
        rows += t.rows.size();
        schemas.push_back(t.schema);
    }
    if (database.tables.empty()) throw std::runtime_error("la base no tiene tablas; carga primero el caso de estudio");
    std::printf("[owner]   %zu tablas y %zu filas leidas en %.2f s\n", database.tables.size(), rows,
                seconds_since(t_read));

    const std::vector<scheme::PlannedTree> plan = scheme::plan_trees(opt.preset, schemas);
    std::printf("[owner] plan '%s': %zu arboles AIT", scheme::preset_name(opt.preset), plan.size());
    if (opt.preset == scheme::Preset::kQueries) {
        std::printf(" (el plan full tendria %zu)", scheme::plan_trees(scheme::Preset::kFull, schemas).size());
    }
    std::printf("\n");

    std::vector<scheme::TreeSpec> specs;
    for (const scheme::PlannedTree& t : plan) specs.push_back(t.spec);
    ProgressPrinter printer(plan);
    const auto t_setup = Clock::now();
    const scheme::SetupResult result =
        scheme::setup(database, specs, scheme::preset_name(opt.preset), sk, pk,
                      [&printer](const scheme::SetupProgress& p) { printer(p); });
    const double setup_seconds = seconds_since(t_setup);

    const auto t_save = Clock::now();
    fs::create_directories(opt.out);
    // D~ primero y delta al final: delta es el que "publica" el estado. Si
    // algo falla a la mitad, nunca queda un delta nuevo junto a un D~ viejo.
    scheme::save_authenticated_db(adb_path, result.database, result.digest);
    scheme::save_digest(delta_path, result.digest);
    const double save_seconds = seconds_since(t_save);

    std::printf("[owner] Setup terminado en %.2f s (filas %.2f s, arboles AIT %.2f s)\n", setup_seconds,
                printer.rows_seconds, printer.trees_seconds);
    std::printf("[owner] archivos guardados en %.2f s; tiempo total del comando %.2f s\n", save_seconds,
                seconds_since(t_total));
    std::printf("[owner] digest delta: %s (%s) -> publico, para el verificador\n", delta_path.string().c_str(),
                file_size_text(delta_path).c_str());
    std::printf("[owner]   version %llu, id %s\n", static_cast<unsigned long long>(result.digest.state_version),
                crypto::to_hex(result.digest.id()).c_str());
    std::printf("[owner] base autenticada D~: %s (%s) -> para el servidor, junto con la base\n",
                adb_path.string().c_str(), file_size_text(adb_path).c_str());
    return 0;
}

}  // namespace tt::owner

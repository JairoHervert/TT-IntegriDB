// publish_command.cpp - comando "tt_owner publish"
//
// Prepara lo que el Propietario entrega a cada modulo, separado por
// carpetas, para que cada uno reciba solo lo que le corresponde:
//
//   <out>/prover/    delta.txt, authenticated_db.bin y public_key.txt
//                    (bloque Servidor: D~ para las pruebas y pk para los
//                    testigos del ASO de C7 a C14)
//   <out>/verifier/  delta.txt y public_key.txt
//                    (bloque Cliente: nunca recibe D~)
//
// La clave secreta sk_D nunca se copia: se queda con el Propietario. Antes
// de copiar se comprueba que los tres archivos correspondan al mismo Setup,
// para no entregar un estado inconsistente.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cli.hpp"
#include "commands.hpp"
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"
#include "tt/scheme/key_io.hpp"
#include "tt/scheme/keys.hpp"

namespace tt::owner {

const char* const kPublishUsage =
    "  tt_owner publish [--keys <carpeta>] [--state <carpeta>] [--out <carpeta>]\n"
    "      Prepara lo que se entrega a cada modulo, en carpetas separadas:\n"
    "        <out>/prover/    delta.txt, authenticated_db.bin y public_key.txt\n"
    "        <out>/verifier/  delta.txt y public_key.txt\n"
    "      La clave secreta nunca se copia. Antes de copiar comprueba que D~ y la\n"
    "      clave publica correspondan a delta. Reemplaza lo que hubiera en esas\n"
    "      carpetas.\n"
    "      --keys <carpeta>   donde esta public_key.txt (por defecto: keys).\n"
    "      --state <carpeta>  donde estan delta.txt y authenticated_db.bin\n"
    "                         (por defecto: setup).\n"
    "      --out <carpeta>    carpeta de salida (por defecto: deploy).\n";

namespace {

namespace fs = std::filesystem;

/*
 * Opciones del comando publish.
 */
struct PublishOptions {
    fs::path keys = "keys";
    fs::path state = "setup";
    fs::path out = "deploy";
};

/*
 * Lee las opciones del comando.
 *
 * Errores:
 *   Lanza UsageError si una opcion es desconocida o le falta su valor.
 */
PublishOptions parse_publish_options(const std::vector<std::string>& args) {
    PublishOptions opt;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--keys") {
            opt.keys = option_value(args, i);
        } else if (arg == "--state") {
            opt.state = option_value(args, i);
        } else if (arg == "--out") {
            opt.out = option_value(args, i);
        } else {
            throw UsageError("opcion desconocida: " + arg);
        }
    }
    return opt;
}

/*
 * Copia un archivo a una carpeta (reemplaza si ya existe) e imprime la
 * linea correspondiente.
 */
void copy_into(const fs::path& file, const fs::path& dir) {
    const fs::path target = dir / file.filename();
    fs::copy_file(file, target, fs::copy_options::overwrite_existing);
    std::printf("[owner]   %-40s %s\n", target.string().c_str(), file_size_text(target).c_str());
}

}  // namespace

/*
 * Implementacion: comprueba primero (delta, D~ contra delta y huella de pk)
 * y solo despues crea las carpetas y copia.
 */
int run_publish(const std::vector<std::string>& args) {
    const PublishOptions opt = parse_publish_options(args);
    const fs::path delta = opt.state / "delta.txt";
    const fs::path adb = opt.state / "authenticated_db.bin";
    const fs::path pk_path = opt.keys / "public_key.txt";
    for (const fs::path& p : {delta, adb, pk_path}) {
        if (!fs::exists(p)) {
            std::fprintf(stderr, "[owner] falta %s: ejecuta primero 'tt_owner init' y 'tt_owner setup'\n",
                         p.string().c_str());
            return 1;
        }
    }

    const auto start = Clock::now();
    const scheme::DatabaseDigest digest = scheme::load_digest(delta);
    scheme::load_authenticated_db(adb, digest);  // lanza si D~ no corresponde a delta
    const scheme::PublicKey pk = scheme::load_public_key(pk_path);
    if (scheme::public_key_fingerprint(pk) != digest.key_fingerprint) {
        std::fprintf(stderr,
                     "[owner] la clave publica de %s no es la que publica %s: no se copia nada.\n"
                     "        Usa la carpeta de claves con la que se ejecuto ese Setup.\n",
                     pk_path.string().c_str(), delta.string().c_str());
        return 1;
    }
    std::printf("[owner] delta, D~ y clave publica corresponden al mismo Setup (version %llu); comprobado en %.2f s\n",
                static_cast<unsigned long long>(digest.state_version), seconds_since(start));

    const fs::path prover_dir = opt.out / "prover";
    const fs::path verifier_dir = opt.out / "verifier";
    fs::create_directories(prover_dir);
    fs::create_directories(verifier_dir);
    std::printf("[owner] para el probador (bloque Servidor):\n");
    copy_into(delta, prover_dir);
    copy_into(adb, prover_dir);
    copy_into(pk_path, prover_dir);
    std::printf("[owner] para el verificador (bloque Cliente):\n");
    copy_into(delta, verifier_dir);
    copy_into(pk_path, verifier_dir);
    std::printf("[owner] la clave secreta se queda en %s y no se entrega a ningun modulo\n",
                opt.keys.string().c_str());
    return 0;
}

}  // namespace tt::owner

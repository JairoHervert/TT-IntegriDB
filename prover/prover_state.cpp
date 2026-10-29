// prover_state.cpp

#include "prover_state.hpp"

#include <string>

#include "tt/scheme/key_io.hpp"

namespace tt::prover {

namespace fs = std::filesystem;

/*
 * Implementacion: revisa que existan los dos archivos antes de leerlos para
 * dar un mensaje claro.
 */
ProverState load_state(const fs::path& state_dir) {
    const fs::path delta_path = state_dir / "delta.txt";
    const fs::path adb_path = state_dir / "authenticated_db.bin";
    if (!fs::exists(delta_path) || !fs::exists(adb_path)) {
        throw StateError("no hay estado autenticado en " + state_dir.string() +
                         ": el propietario debe ejecutar 'tt_owner setup' y entregar delta.txt y "
                         "authenticated_db.bin (por ejemplo con 'tt_owner publish').");
    }
    ProverState state;
    state.digest = scheme::load_digest(delta_path);
    state.database = scheme::load_authenticated_db(adb_path, state.digest);
    return state;
}

/*
 * Implementacion: la huella se calcula sobre la clave completa, por lo que
 * una clave de otra ejecucion de Init se detecta aqui.
 */
void load_public_key(ProverState& state, const fs::path& keys_dir) {
    const fs::path pk_path = keys_dir / "public_key.txt";
    if (!fs::exists(pk_path)) {
        throw StateError("no hay clave publica en " + pk_path.string() +
                         "; las consultas C7 a C14 la necesitan para generar los testigos del ASO.");
    }
    scheme::PublicKey pk = scheme::load_public_key(pk_path);
    if (scheme::public_key_fingerprint(pk) != state.digest.key_fingerprint) {
        throw StateError("la clave publica de " + pk_path.string() +
                         " no es la que publica delta: usa la del mismo 'tt_owner init' con el que se ejecuto Setup.");
    }
    state.public_key = std::move(pk);
}

}  // namespace tt::prover

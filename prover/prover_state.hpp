// prover_state.hpp
//
// Lo que el Propietario entrega al probador y que este carga antes de
// responder: delta, D~ y, para las consultas C7 a C14, la clave publica.
// Lo comparten los comandos answer (una consulta) y serve (servidor).

#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>

#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"
#include "tt/scheme/keys.hpp"

namespace tt::prover {

// Estado cargado del probador.
struct ProverState {
    scheme::DatabaseDigest digest;              // delta
    scheme::AuthenticatedDatabase database;     // D~ (ya comprobada contra delta)
    std::optional<scheme::PublicKey> public_key;  // solo si se cargo
};

// El estado no existe o no es consistente. El mensaje explica que hacer.
class StateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Carga delta y D~ de la carpeta del estado; D~ se comprueba contra delta al
 * cargarse (suma de verificacion, id de delta y raices).
 *
 * Parametros:
 *   state_dir  carpeta con delta.txt y authenticated_db.bin
 *
 * Devuelve:
 *   El estado, todavia sin clave publica.
 *
 * Errores:
 *   Lanza StateError si faltan los archivos; deja pasar los errores de
 *   formato o de consistencia de delta y D~.
 */
ProverState load_state(const std::filesystem::path& state_dir);

/*
 * Carga la clave publica y exige que su huella sea la que publica delta.
 *
 * Parametros:
 *   state     estado ya cargado (recibe la clave)
 *   keys_dir  carpeta con public_key.txt
 *
 * Errores:
 *   Lanza StateError si el archivo no existe o si la huella no coincide con
 *   la de delta.
 */
void load_public_key(ProverState& state, const std::filesystem::path& keys_dir);

}  // namespace tt::prover

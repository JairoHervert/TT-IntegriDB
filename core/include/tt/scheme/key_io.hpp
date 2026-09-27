// key_io.hpp
//
// Archivos de claves. Los tres programas los comparten: el propietario
// escribe pk y sk_D; el probador y el verificador leen pk.
//
// Formato de texto por lineas, con encabezado versionado y los puntos en
// coordenadas afines en decimal:
//
//   tt-integridb public-key v1        tt-integridb secret-key v1
//   curve bn254-fp254bnb              curve bn254-fp254bnb
//   q <q>                             s <decimal>
//   g1_s_powers                       a <decimal>
//   <x> <y>          (q + 1 lineas)   enc_key <32 digitos hex>
//   g2_s_powers                       end
//   <xa> <xb> <ya> <yb>  (q + 1)
//   g1_as_powers
//   <x> <y>          (q + 1 lineas)
//   g2_a
//   <xa> <xb> <ya> <yb>
//   end
//
// La lectura es estricta: cualquier cosa fuera de este formato lanza
// KeyFormatError indicando la linea. No se usa el lector de numeros de
// ate-pairing directamente porque, ante ciertas entradas (por ejemplo un
// numero con cero a la izquierda), termina el proceso con exit(1) en lugar
// de lanzar una excepcion.

#pragma once

#include <filesystem>
#include <iosfwd>
#include <stdexcept>

#include "tt/scheme/keys.hpp"

namespace tt::scheme {

class KeyFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// --- Flujos ------------------------------------------------------------------

void write_public_key(std::ostream& out, const PublicKey& pk);

// Ademas del formato, revisa que todos los puntos esten en la curva y sean
// distintos de O, que las potencias 0 sean los generadores y que g2^s y g2^a
// (los que usa el verificador) esten en el subgrupo de orden r. La revision
// completa de la estructura es aso::is_well_formed (mas costosa).
PublicKey read_public_key(std::istream& in);

void write_secret_key(std::ostream& out, const SecretKey& sk);
SecretKey read_secret_key(std::istream& in);

// --- Archivos ----------------------------------------------------------------

// Escribe en un archivo temporal y luego lo renombra, para que nunca quede
// un archivo de clave a medio escribir.
void save_public_key(const std::filesystem::path& path, const PublicKey& pk);
PublicKey load_public_key(const std::filesystem::path& path);

enum class Overwrite { kNo, kYes };

// Crea el archivo con permisos 0600 (solo el dueno puede leerlo). Con
// Overwrite::kNo falla si el archivo ya existe: sobrescribir sk_D haria
// imposible actualizar la base autenticada que se construyo con la anterior.
void save_secret_key(const std::filesystem::path& path, const SecretKey& sk,
                     Overwrite overwrite = Overwrite::kNo);
SecretKey load_secret_key(const std::filesystem::path& path);

}  // namespace tt::scheme

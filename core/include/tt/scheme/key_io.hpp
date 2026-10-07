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

// Error de formato en un archivo de clave; el mensaje indica la linea.
class KeyFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// --- Flujos ------------------------------------------------------------------

/*
 * Escribe la clave publica en un flujo con el formato de texto descrito
 * arriba. Los puntos se escriben en coordenadas afines, asi que la misma
 * clave produce siempre exactamente el mismo texto.
 *
 * Parametros:
 *   out  flujo de salida (archivo, stringstream, ...)
 *   pk   clave publica
 */
void write_public_key(std::ostream& out, const PublicKey& pk);

/*
 * Lee una clave publica escrita por write_public_key.
 *
 * Ademas del formato, revisa que todos los puntos esten en la curva y sean
 * distintos de O, que las potencias 0 sean los generadores y que g2^s y g2^a
 * (los que usa el verificador) esten en el subgrupo de orden r. La revision
 * completa de la estructura es aso::is_well_formed (mas costosa).
 *
 * Parametros:
 *   in  flujo de entrada
 *
 * Devuelve:
 *   La clave publica.
 *
 * Errores:
 *   Lanza KeyFormatError, con el numero de linea, ante cualquier desviacion
 *   del formato o punto invalido. Acepta fines de linea de Windows (CRLF).
 */
PublicKey read_public_key(std::istream& in);

/*
 * Escribe la clave secreta sk_D (s, a y la clave AES) en un flujo. El texto
 * intermedio se borra de memoria despues de escribirlo.
 *
 * Parametros:
 *   out  flujo de salida
 *   sk   clave secreta
 */
void write_secret_key(std::ostream& out, const SecretKey& sk);

/*
 * Lee una clave secreta escrita por write_secret_key. Exige que s y a esten
 * entre 1 y r - 1 y que la clave AES tenga exactamente 32 digitos
 * hexadecimales.
 *
 * Parametros:
 *   in  flujo de entrada
 *
 * Devuelve:
 *   La clave secreta.
 *
 * Errores:
 *   Lanza KeyFormatError, con el numero de linea, si el formato no es valido.
 */
SecretKey read_secret_key(std::istream& in);

// --- Archivos ----------------------------------------------------------------

/*
 * Guarda la clave publica en un archivo. Escribe primero en un archivo
 * temporal (<ruta>.tmp) y luego lo renombra, para que nunca quede un archivo
 * de clave a medio escribir; si algo falla, borra el temporal.
 *
 * Parametros:
 *   path  ruta del archivo (se reemplaza si existe)
 *   pk    clave publica
 *
 * Errores:
 *   Lanza std::runtime_error o std::filesystem::filesystem_error si no se
 *   puede escribir.
 */
void save_public_key(const std::filesystem::path& path, const PublicKey& pk);

/*
 * Carga una clave publica desde un archivo (ver read_public_key).
 *
 * Errores:
 *   Lanza std::runtime_error si el archivo no se puede abrir y
 *   KeyFormatError, con la ruta y la linea, si el contenido no es valido.
 */
PublicKey load_public_key(const std::filesystem::path& path);

// Si save_secret_key puede reemplazar un archivo existente.
enum class Overwrite { kNo, kYes };

/*
 * Guarda la clave secreta sk_D en un archivo con permisos 0600 (solo el
 * dueno puede leerlo). Si el archivo ya existia y se reemplaza, tambien le
 * fuerza esos permisos. Si la escritura falla a medias, borra el archivo.
 *
 * Parametros:
 *   path       ruta del archivo
 *   sk         clave secreta
 *   overwrite  con Overwrite::kNo (por defecto) falla si el archivo ya
 *              existe: sobrescribir sk_D haria imposible actualizar la base
 *              autenticada que se construyo con la clave anterior.
 *
 * Errores:
 *   Lanza std::runtime_error si el archivo existe y no se permite
 *   sobrescribirlo, y std::system_error ante errores del sistema de archivos.
 */
void save_secret_key(const std::filesystem::path& path, const SecretKey& sk,
                     Overwrite overwrite = Overwrite::kNo);

/*
 * Carga una clave secreta desde un archivo (ver read_secret_key).
 *
 * Errores:
 *   Lanza std::runtime_error si el archivo no se puede abrir y
 *   KeyFormatError, con la ruta y la linea, si el contenido no es valido.
 */
SecretKey load_secret_key(const std::filesystem::path& path);

}  // namespace tt::scheme

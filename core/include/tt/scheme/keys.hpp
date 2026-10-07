// keys.hpp
//
// Claves del esquema de verificacion y algoritmo Init (seccion
// "Inicializacion del esquema" del reporte):
//
//     (s, pk_ASO) <- ASOInit(1^lambda)
//     elegir una clave sk para el cifrado Enc del AIT
//     sk_D <- (s, sk),   pk <- pk_ASO
//
// En la implementacion, la parte secreta del ASO incluye ademas el exponente
// de extractabilidad a (ver tt/aso/keys.hpp).

#pragma once

#include <cstddef>

#include "tt/aso/keys.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/crypto/symmetric.hpp"

namespace tt::scheme {

// Clave del cifrado simetrico Enc que usa el AIT: AES-CBC-128 (reporte).
constexpr std::size_t kEncKeyBytes = crypto::kAesKeyBytes;
using EncKey = crypto::AesKey;

// sk_D: la conserva el Propietario de datos y nunca se entrega al servidor.
struct SecretKey {
    aso::SecretKey aso;  // s y a
    EncKey enc_key;      // sk del cifrado Enc
};

// pk: la reciben el modulo probador y el modulo verificador.
struct PublicKey {
    aso::PublicKey aso;
};

// Par de claves que produce init().
struct KeyPair {
    SecretKey sk;
    PublicKey pk;
};

/*
 * Algoritmo Init del reporte: genera todas las claves del esquema. Ejecuta
 * ASOInit (s, a y la clave publica del ASO) y elige al azar la clave AES del
 * cifrado Enc que usa el AIT.
 *
 * Parametros:
 *   q  cota superior del tamano de cualquier conjunto autenticado (ver
 *      aso::init).
 *
 * Devuelve:
 *   sk_D para el propietario y pk para el probador y el verificador.
 *
 * Errores:
 *   Lanza std::invalid_argument si q esta fuera de rango y
 *   std::runtime_error si falla la fuente de aleatoriedad.
 */
KeyPair init(std::size_t q);

/*
 * Huella de la clave publica: un hash corto que identifica exactamente
 * esa clave. El digest delta la incluye para que el verificador compruebe
 * que tiene la misma pk con la que se construyo el estado (y no una de
 * otro Init, o un archivo alterado) en lugar de rechazar cada respuesta
 * sin explicacion.
 *
 * Es SHA-256 con la etiqueta 0x21 sobre q y todos los puntos de pk en
 * codificacion canonica: las potencias g1^{s^i}, g2^{s^i}, g1^{a s^i} y
 * g2^a. Cambiar cualquier punto cambia la huella. Con q = 16000 son unos
 * 48 mil puntos y tarda del orden de decenas de milisegundos.
 *
 * Parametros:
 *   pk  clave publica
 *
 * Devuelve:
 *   Los 32 bytes de la huella.
 *
 * Errores:
 *   Lanza std::invalid_argument si pk no tiene al menos las potencias de
 *   grado 1 (q >= 1) o si sus listas no tienen el mismo tamano.
 */
crypto::Digest public_key_fingerprint(const PublicKey& pk);

/*
 * Comprueba que la clave publica corresponda a la clave secreta:
 * g1^s, g2^s y g2^a deben ser los que da la clave secreta. Evita que el
 * propietario ejecute Setup con un pk de otras claves (el delta resultante
 * no le serviria a ningun verificador).
 *
 * Devuelve:
 *   true si pk y sk son del mismo par.
 */
bool keys_match(const SecretKey& sk, const PublicKey& pk);

}  // namespace tt::scheme

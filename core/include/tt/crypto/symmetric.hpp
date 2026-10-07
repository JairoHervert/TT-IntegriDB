// symmetric.hpp
//
// Cifrado Enc_sk del reporte: AES-128-CBC.
//
// El AIT guarda en cada nodo Enc_sk(e_u), donde e_u es el exponente del
// acumulador del subarbol. Ese exponente NO puede quedar en claro en el
// servidor: e_u = prod (v^{-1} + s) es una ecuacion polinomial en s, y
// conociendo e_u y los valores del subarbol se podria despejar s.
//
// Detalles de la construccion (sigue siendo AES-128-CBC):
//   - IV sintetico: IV = HMAC-SHA256(k_iv, mensaje)[0..16). El mismo mensaje
//     con la misma clave da siempre el mismo cifrado, asi que el mismo
//     estado de la base con las mismas claves produce siempre el mismo AIT y
//     el mismo digest (experimentos reproducibles). Solo revela si dos
//     mensajes son iguales, cosa que el acumulador g^{e_u} ya revela.
//   - Integridad: al descifrar se recalcula el IV a partir del mensaje y se
//     compara; si el servidor altero el cifrado, se detecta. La idea es la de
//     SIV (RFC 5297), adaptada a HMAC + CBC.
//   - Separacion de claves: la clave del propietario sk solo se usa como
//     clave HMAC para derivar k_aes y k_iv; nunca se usa la misma clave para
//     dos primitivas distintas.

#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace tt::crypto {

constexpr std::size_t kAesKeyBytes = 16;    // AES-128
constexpr std::size_t kAesBlockBytes = 16;
using AesKey = std::array<unsigned char, kAesKeyBytes>;

// Error al descifrar: formato invalido o cifrado alterado.
class DecryptionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Cifra un mensaje con AES-128-CBC usando un IV sintetico (derivado de la
 * clave y del propio mensaje). Es el Enc_sk del reporte.
 *
 * Por ser determinista, el mismo mensaje con la misma clave produce siempre
 * el mismo resultado.
 *
 * Parametros:
 *   key        clave sk del propietario (16 bytes)
 *   plaintext  mensaje a cifrar (puede estar vacio)
 *
 * Devuelve:
 *   IV (16 bytes) seguido del cifrado con relleno PKCS#7 (un bloque mas que
 *   el mensaje redondeado a bloques de 16).
 *
 * Errores:
 *   Lanza std::runtime_error si OpenSSL falla.
 */
std::vector<unsigned char> encrypt_deterministic(const AesKey& key,
                                                 const std::vector<unsigned char>& plaintext);

/*
 * Descifra lo que produjo encrypt_deterministic y comprueba que no haya sido
 * alterado: recalcula el IV a partir del mensaje descifrado y lo compara con
 * el IV recibido.
 *
 * Parametros:
 *   key     clave sk del propietario
 *   sealed  IV seguido del cifrado
 *
 * Devuelve:
 *   El mensaje original.
 *
 * Errores:
 *   Lanza DecryptionError si la longitud es invalida, si el relleno no es
 *   valido, si la clave es otra o si cualquier byte fue modificado.
 */
std::vector<unsigned char> decrypt_deterministic(const AesKey& key,
                                                 const std::vector<unsigned char>& sealed);

}  // namespace tt::crypto

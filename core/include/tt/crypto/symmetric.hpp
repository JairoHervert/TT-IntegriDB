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

class DecryptionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Devuelve IV || C.
std::vector<unsigned char> encrypt_deterministic(const AesKey& key,
                                                 const std::vector<unsigned char>& plaintext);

// Recibe IV || C. Lanza DecryptionError si el formato es invalido o si el
// cifrado fue alterado.
std::vector<unsigned char> decrypt_deterministic(const AesKey& key,
                                                 const std::vector<unsigned char>& sealed);

}  // namespace tt::crypto

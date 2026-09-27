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

struct KeyPair {
    SecretKey sk;
    PublicKey pk;
};

// Algoritmo Init. `q` es la cota superior del tamano de cualquier conjunto
// autenticado (ver aso::init).
KeyPair init(std::size_t q);

}  // namespace tt::scheme

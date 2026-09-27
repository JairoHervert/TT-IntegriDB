// random.hpp
//
// Aleatoriedad para material secreto (s, a, clave AES).
//
// No se usa NTL::random() para esto: si nadie le fija una semilla, NTL usa
// una semilla por defecto que solo es "unica por proceso", no una fuente
// criptografica. Aqui todo sale de RAND_bytes de OpenSSL, que toma entropia
// del sistema operativo.

#pragma once

#include <NTL/ZZ_p.h>

#include <cstddef>
#include <vector>

namespace tt::crypto {

// `count` bytes de un generador criptograficamente seguro.
std::vector<unsigned char> random_bytes(std::size_t count);

// Elemento uniforme de Z_r distinto de cero.
NTL::ZZ_p random_nonzero_scalar();

}  // namespace tt::crypto

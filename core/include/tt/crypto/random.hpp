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

/*
 * Genera bytes aleatorios con un generador criptograficamente seguro
 * (RAND_bytes de OpenSSL). Es la unica fuente de aleatoriedad para material
 * secreto en todo el proyecto.
 *
 * Parametros:
 *   count  cuantos bytes generar (puede ser 0)
 *
 * Devuelve:
 *   Vector de `count` bytes aleatorios.
 *
 * Errores:
 *   Lanza std::runtime_error si el sistema no puede dar aleatoriedad segura.
 */
std::vector<unsigned char> random_bytes(std::size_t count);

/*
 * Genera un elemento de Z_r uniforme y distinto de cero. Se usa para los
 * secretos s y a del propietario y para los coeficientes aleatorios de las
 * comprobaciones por lotes.
 *
 * Devuelve:
 *   Elemento aleatorio de Z_r, nunca cero.
 *
 * Errores:
 *   Lanza std::runtime_error si falla la fuente de aleatoriedad.
 */
NTL::ZZ_p random_nonzero_scalar();

}  // namespace tt::crypto

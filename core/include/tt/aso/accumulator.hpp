// accumulator.hpp
//
// Acumulador de conjuntos del bloque ASO:
//
//     acc(S) = g^{P_S(s)},   con   P_S(X) = prod_{x in S} (X + x)
//
// P_S es el "polinomio caracteristico" de S: sus raices son los negativos
// de los elementos. Dos conjuntos distintos tienen polinomios distintos y,
// sin conocer s, encontrar otro conjunto con el mismo acumulador es
// computacionalmente inviable bajo la suposicion q-SDH del reporte (el
// articulo de IntegriDB usa su variante bilineal, q-SBDH).
//
// Los conjuntos se tratan como multiconjuntos: el orden no importa y un
// elemento repetido cuenta con su multiplicidad, como en el ASO de
// IntegriDB (que soporta multiconjuntos para SUM).
//
// Cada funcion tiene dos versiones: sobre elementos de Z_r (FieldSet) y, por
// comodidad, sobre enteros (se convierten con to_field).

#pragma once

#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pX.h>

#include <cstdint>
#include <vector>

#include "bn.h"
#include "tt/aso/keys.hpp"

namespace tt::aso {

// Elemento de un conjunto autenticado. En el esquema son identificadores
// de fila (record_id, student_id, ...), que en MySQL son INT.
using Element = std::int64_t;

/*
 * Convierte un entero en elemento de Z_r. Los negativos se reducen modulo r
 * (por ejemplo, -5 se vuelve r - 5).
 *
 * Parametros:
 *   x  entero de 64 bits
 *
 * Devuelve:
 *   El valor en Z_r.
 */
NTL::ZZ_p to_field(Element x);

// Conjunto ya expresado como elementos de Z_r. El AIT, por ejemplo,
// acumula los inversos v^{-1} de los valores, no los valores mismos. Es un
// tipo propio (y no un alias de std::vector) para que una lista de enteros
// como {3, 7} nunca se confunda con una lista de elementos de Z_r.
struct FieldSet {
    std::vector<NTL::ZZ_p> elements;
};

/*
 * Convierte un conjunto de enteros en un FieldSet, elemento por elemento con
 * to_field.
 */
FieldSet to_field(const std::vector<Element>& set);

/*
 * Calcula el polinomio caracteristico P_S(X) = prod (X + x) del conjunto.
 * Sus coeficientes son los que el probador combina con las potencias
 * publicas para obtener el acumulador sin conocer s.
 *
 * Parametros:
 *   set  conjunto (multiconjunto) de elementos de Z_r
 *
 * Devuelve:
 *   Polinomio monico de grado |S|; el polinomio 1 si el conjunto es vacio.
 */
NTL::ZZ_pX characteristic_polynomial(const FieldSet& set);

/*
 * Igual que la version anterior, para un conjunto de enteros.
 */
NTL::ZZ_pX characteristic_polynomial(const std::vector<Element>& set);

/*
 * Evalua el polinomio caracteristico en s: P_S(s) = prod (x + s). Es el
 * exponente del acumulador. Solo lo puede calcular quien conoce s.
 *
 * Parametros:
 *   set  conjunto de elementos de Z_r
 *   s    secreto del propietario
 *
 * Devuelve:
 *   El producto; 1 si el conjunto es vacio.
 */
NTL::ZZ_p characteristic_value(const FieldSet& set, const NTL::ZZ_p& s);

/*
 * Igual que la version anterior, para un conjunto de enteros.
 */
NTL::ZZ_p characteristic_value(const std::vector<Element>& set, const NTL::ZZ_p& s);

// --- Lo que calcula el probador: solo con la clave publica ---------------

/*
 * Calcula el acumulador acc(S) en G1 usando solo la clave publica: expande
 * P_S(X) = c_0 + c_1 X + ... + c_n X^n y combina c_0 g1 + c_1 g1^s + ... +
 * c_n g1^{s^n}. Es lo que hace el probador; equivale a compute_digest_pub()
 * del original.
 *
 * Parametros:
 *   set  conjunto de elementos de Z_r
 *   pk   clave publica del ASO
 *
 * Devuelve:
 *   g1^{P_S(s)}; g1 si el conjunto es vacio.
 *
 * Errores:
 *   Lanza std::length_error si |S| > pk.q() y std::invalid_argument si la
 *   clave publica esta vacia o incompleta.
 */
bn::Ec1 accumulate_g1(const FieldSet& set, const PublicKey& pk);

/*
 * Igual que accumulate_g1, pero en G2 (con las potencias g2^{s^i}). El
 * probador lo necesita para los testigos de las pruebas de conjuntos.
 */
bn::Ec2 accumulate_g2(const FieldSet& set, const PublicKey& pk);

/*
 * accumulate_g1 para un conjunto de enteros.
 */
bn::Ec1 accumulate_g1(const std::vector<Element>& set, const PublicKey& pk);

/*
 * accumulate_g2 para un conjunto de enteros.
 */
bn::Ec2 accumulate_g2(const std::vector<Element>& set, const PublicKey& pk);

// --- Lo que calcula el propietario: con el secreto s ---------------------

/*
 * Calcula el mismo acumulador g1^{P_S(s)} usando el secreto s: evalua el
 * producto y hace una sola multiplicacion escalar, mucho mas rapido que la
 * version con la clave publica. Lo usa el propietario en Setup para los
 * nodos del AIT. No tiene limite de tamano porque no usa las potencias.
 *
 * Parametros:
 *   set  conjunto de elementos de Z_r
 *   sk   clave secreta del ASO
 *
 * Devuelve:
 *   g1^{P_S(s)}.
 */
bn::Ec1 accumulate_g1_with_secret(const FieldSet& set, const SecretKey& sk);

/*
 * Igual que accumulate_g1_with_secret, pero en G2.
 */
bn::Ec2 accumulate_g2_with_secret(const FieldSet& set, const SecretKey& sk);

/*
 * accumulate_g1_with_secret para un conjunto de enteros.
 */
bn::Ec1 accumulate_g1_with_secret(const std::vector<Element>& set, const SecretKey& sk);

/*
 * accumulate_g2_with_secret para un conjunto de enteros.
 */
bn::Ec2 accumulate_g2_with_secret(const std::vector<Element>& set, const SecretKey& sk);

}  // namespace tt::aso

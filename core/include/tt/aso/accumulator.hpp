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

// Elemento como valor de Z_r (los negativos se reducen modulo r).
NTL::ZZ_p to_field(Element x);

// Conjunto ya expresado como elementos de Z_r. El AIT, por ejemplo,
// acumula los inversos v^{-1} de los valores, no los valores mismos. Es un
// tipo propio (y no un alias de std::vector) para que una lista de enteros
// como {3, 7} nunca se confunda con una lista de elementos de Z_r.
struct FieldSet {
    std::vector<NTL::ZZ_p> elements;
};
FieldSet to_field(const std::vector<Element>& set);

// Cada funcion tiene dos versiones: sobre elementos de Z_r (FieldSet) y,
// por comodidad, sobre enteros (se convierten con to_field).

// P_S(X) = prod (X + x). Para el conjunto vacio es el polinomio 1.
NTL::ZZ_pX characteristic_polynomial(const FieldSet& set);
NTL::ZZ_pX characteristic_polynomial(const std::vector<Element>& set);

// P_S(s) = prod (x + s). Para el conjunto vacio es 1.
NTL::ZZ_p characteristic_value(const FieldSet& set, const NTL::ZZ_p& s);
NTL::ZZ_p characteristic_value(const std::vector<Element>& set, const NTL::ZZ_p& s);

// --- Lo que calcula el probador: solo con la clave publica ---------------
// Expande P_S(X) = c_0 + c_1 X + ... + c_n X^n y combina
// sum c_i g^{s^i}. Equivale a compute_digest_pub() del original.
// Lanza std::length_error si |S| > pk.q().
bn::Ec1 accumulate_g1(const FieldSet& set, const PublicKey& pk);
bn::Ec2 accumulate_g2(const FieldSet& set, const PublicKey& pk);
bn::Ec1 accumulate_g1(const std::vector<Element>& set, const PublicKey& pk);
bn::Ec2 accumulate_g2(const std::vector<Element>& set, const PublicKey& pk);

// --- Lo que calcula el propietario: con el secreto s ---------------------
// Una sola multiplicacion escalar, mucho mas rapido. Se usa en Setup para
// construir los digests de los nodos del AIT.
bn::Ec1 accumulate_g1_with_secret(const FieldSet& set, const SecretKey& sk);
bn::Ec2 accumulate_g2_with_secret(const FieldSet& set, const SecretKey& sk);
bn::Ec1 accumulate_g1_with_secret(const std::vector<Element>& set, const SecretKey& sk);
bn::Ec2 accumulate_g2_with_secret(const std::vector<Element>& set, const SecretKey& sk);

}  // namespace tt::aso

// pairing_group.hpp
//
// Grupo bilineal del esquema: curva BN254 (CurveFp254BNb) de ate-pairing,
// la misma que usa IntegriDB, y el campo Z_r de NTL para los exponentes.
//
// Los tres programas (owner, prover, verifier) necesitan exactamente la misma
// configuracion; en el IntegriDB original se hacia dentro del main() de
// client.cpp. Aqui vive una sola vez, en el nucleo.

#pragma once

#include <NTL/ZZ_p.h>

#include <cstddef>
#include <vector>

#include "bn.h"

namespace tt::crypto {

// Generadores g1 de G1 y g2 de G2.
struct PairingGroup {
    bn::Ec1 g1;
    bn::Ec2 g2;
};

// Inicializa ate-pairing y el modulo de NTL (Z_r) y devuelve los
// generadores. Se puede llamar las veces que sea; ate-pairing se inicializa
// una sola vez por proceso y el modulo de NTL una vez por hilo (NTL guarda
// el modulo por hilo). Las funciones del nucleo que hacen aritmetica en Z_r
// la llaman por su cuenta, asi que no hay que acordarse de llamarla antes.
const PairingGroup& init_pairing_group();

// Orden r de los grupos G1, G2 y GT, en decimal (254 bits).
const char* group_order_decimal();

// Emparejamiento e: G1 x G2 -> GT, en el mismo orden de argumentos que el
// reporte (ate-pairing los recibe al reves).
bn::Fp12 pairing(const bn::Ec1& p, const bn::Ec2& q);

// Convierte un elemento de Z_r (NTL) al entero que usa ate-pairing para
// multiplicar puntos: g1 * to_vuint(x). Equivale a zToString() de IntegriDB.
mie::Vuint to_vuint(const NTL::ZZ_p& z);

// Elemento de Z_r como 32 bytes big-endian, y de regreso. La lectura lanza
// std::invalid_argument si no son 32 bytes o si el valor no es menor que r.
constexpr std::size_t kScalarBytes = 32;
std::vector<unsigned char> scalar_to_bytes(const NTL::ZZ_p& z);
NTL::ZZ_p scalar_from_bytes(const std::vector<unsigned char>& bytes);

}  // namespace tt::crypto

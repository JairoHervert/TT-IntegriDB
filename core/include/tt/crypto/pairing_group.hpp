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

#include "bn.h"

namespace tt::crypto {

// Generadores g1 de G1 y g2 de G2.
struct PairingGroup {
    bn::Ec1 g1;
    bn::Ec2 g2;
};

// Inicializa ate-pairing y el modulo de NTL (Z_r) y devuelve los
// generadores. Se puede llamar varias veces; solo inicializa la primera.
//
// Nota: el modulo de NTL es por hilo. Si mas adelante el probador atiende
// consultas en varios hilos, cada hilo debe volver a fijar el modulo.
const PairingGroup& init_pairing_group();

// Orden r de los grupos G1, G2 y GT, en decimal (254 bits).
const char* group_order_decimal();

// Emparejamiento e: G1 x G2 -> GT, en el mismo orden de argumentos que el
// reporte (ate-pairing los recibe al reves).
bn::Fp12 pairing(const bn::Ec1& p, const bn::Ec2& q);

// Convierte un elemento de Z_r (NTL) al entero que usa ate-pairing para
// multiplicar puntos: g1 * to_vuint(x). Equivale a zToString() de IntegriDB.
mie::Vuint to_vuint(const NTL::ZZ_p& z);

}  // namespace tt::crypto

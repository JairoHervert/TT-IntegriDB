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

/*
 * Prepara todo lo necesario para hacer criptografia con emparejamientos y
 * devuelve los generadores g1 y g2.
 *
 * Inicializa ate-pairing con la curva BN254 y fija el modulo de NTL en el
 * orden r del grupo, para que los exponentes que se calculan con NTL
 * correspondan a los del grupo. Se puede llamar las veces que sea:
 * ate-pairing se inicializa una sola vez por proceso y el modulo de NTL una
 * vez por hilo (NTL lo guarda por hilo). Las funciones del nucleo que hacen
 * aritmetica en Z_r la llaman por su cuenta, asi que no hay que acordarse de
 * llamarla antes.
 *
 * Devuelve:
 *   Referencia a los generadores; es la misma en todas las llamadas.
 *
 * Errores:
 *   Lanza std::runtime_error si el orden r de ate-pairing no coincide con el
 *   modulo esperado o si los generadores no estan en la curva (indicaria una
 *   version incompatible de la biblioteca).
 */
const PairingGroup& init_pairing_group();

/*
 * Devuelve el orden r de los grupos G1, G2 y GT como texto en decimal
 * (254 bits). Sirve para comparar y validar escalares leidos de archivos.
 */
const char* group_order_decimal();

/*
 * Calcula el emparejamiento bilineal e(p, q) de G1 x G2 en GT.
 *
 * Recibe los argumentos en el mismo orden que el reporte (primero el punto
 * de G1); ate-pairing los recibe al reves y esta funcion se encarga de
 * voltearlos para evitar errores.
 *
 * Parametros:
 *   p  punto de G1
 *   q  punto de G2
 *
 * Devuelve:
 *   El elemento e(p, q) de GT.
 */
bn::Fp12 pairing(const bn::Ec1& p, const bn::Ec2& q);

/*
 * Convierte un elemento de Z_r (tipo de NTL) al entero que usa ate-pairing
 * para multiplicar puntos, por ejemplo g1 * to_vuint(x). Equivale a
 * zToString() del IntegriDB original, sin su fuga de memoria.
 *
 * Parametros:
 *   z  elemento de Z_r
 *
 * Devuelve:
 *   El mismo valor como entero de ate-pairing.
 */
mie::Vuint to_vuint(const NTL::ZZ_p& z);

// Tamano de un elemento de Z_r escrito como bytes.
constexpr std::size_t kScalarBytes = 32;

/*
 * Escribe un elemento de Z_r como 32 bytes en orden big-endian (el byte mas
 * significativo primero). Se usa para cifrar exponentes con AES y para
 * guardarlos en archivos.
 *
 * Parametros:
 *   z  elemento de Z_r
 *
 * Devuelve:
 *   Vector de exactamente 32 bytes.
 */
std::vector<unsigned char> scalar_to_bytes(const NTL::ZZ_p& z);

/*
 * Operacion inversa de scalar_to_bytes: lee 32 bytes big-endian como
 * elemento de Z_r, validando que el valor sea canonico (menor que r).
 *
 * Parametros:
 *   bytes  exactamente 32 bytes
 *
 * Devuelve:
 *   El elemento de Z_r.
 *
 * Errores:
 *   Lanza std::invalid_argument si no son 32 bytes o si el valor no es menor
 *   que r.
 */
NTL::ZZ_p scalar_from_bytes(const std::vector<unsigned char>& bytes);

}  // namespace tt::crypto

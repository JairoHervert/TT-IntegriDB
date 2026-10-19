#pragma once

// Certificado ASO de interseccion entre dos rangos AIT.
// Para cada nodo autenticado N, separa su conjunto en C_N (identificadores
// publicados del resultado) y D_N (complemento). Demuestra
// P_N(s)=P_C_N(s)P_D_N(s) mediante el pairing de sus acumuladores.
// Para cada par de complementos (D_i, E_j) prueba que sus polinomios son
// coprimos usando la identidad de Bezout U P_D_i + V P_E_j = 1.
// La prueba usa solo la clave publica (sin s), y el verificador no necesita
// conocer los identificadores de los complementos.

#include <vector>
#include "tt/aso/accumulator.hpp"

namespace tt::aso {
/*
 * Testigo ASO de la separacion de un nodo autenticado N en dos partes:
 * identificadores publicados C y complemento D. Guarda los acumuladores de
 * D en G1 y G2 para las comprobaciones con emparejamientos bilineales.
 */
struct SplitWitness {
    bn::Ec1 rest_g1;
    bn::Ec2 rest_g2;
};
/*
 * Testigo ASO de coprimalidad de dos complementos D y E.
 * bezout_u y bezout_v representan en G2 los polinomios U(s) y V(s) que
 * satisfacen U(X)*P_D(X) + V(X)*P_E(X) = 1.
 */
struct DisjointWitness {
    bn::Ec2 bezout_u;
    bn::Ec2 bezout_v;
};

/*
 * Genera los acumuladores de un complemento D para demostrar la separacion
 * de un nodo AIT en identificadores publicados C y complemento D.
 * No necesita la clave secreta ASO.
 *
 * Parametros:
 *   complement  identificadores restantes, ya expresados en Z_r
 *   pk          clave publica ASO con las potencias necesarias
 *
 * Devuelve:
 *   SplitWitness con acc_G1(D) y acc_G2(D).
 *
 * Errores:
 *   Propaga los errores de accumulate_g1 y accumulate_g2 si D supera
 *   la capacidad de la clave publica o esta incompleta.
 */
SplitWitness prove_split(const FieldSet& complement, const PublicKey& pk);
/*
 * Verifica que la parte publicada C y el complemento D sean consistentes
 * con el acumulador autenticado de un nodo N del AIT.
 * Comprueba la consistencia de D entre G1 y G2 y la igualdad de pairings
 * correspondiente a P_C(s)*P_D(s) = P_N(s).
 *
 * Parametros:
 *   authenticated      acumulador en G1 del nodo N autenticado por el AIT
 *   intersection_part  identificadores de C expresados en Z_r
 *   proof              acumuladores recibidos para el complemento D
 *   pk                 clave publica ASO
 *
 * Devuelve:
 *   true si las comprobaciones criptograficas pasan; false en otro caso.
 *
 * Errores:
 *   Puede propagar errores de accumulate_g1 si la parte publicada excede
 *   la capacidad de la clave publica o la clave esta incompleta.
 */
bool verify_split(const bn::Ec1& authenticated, const FieldSet& intersection_part,
                  const SplitWitness& proof, const PublicKey& pk);
/*
 * Construye un certificado de que dos complementos son disjuntos.
 * Calcula el MCD extendido de sus polinomios caracteristicos con NTL::XGCD
 * y convierte los coeficientes de Bezout en testigos del grupo G2.
 *
 * Parametros:
 *   a   primer complemento, representado en Z_r
 *   b   segundo complemento, representado en Z_r
 *   pk  clave publica ASO para evaluar los testigos sin conocer s
 *
 * Devuelve:
 *   DisjointWitness con los compromisos de U(s) y V(s) en G2.
 *
 * Errores:
 *   Lanza std::invalid_argument si los polinomios no son coprimos y
 *   std::length_error si alguno de los testigos supera las potencias
 *   disponibles de la clave publica; propaga errores de acumulacion.
 */
DisjointWitness prove_disjoint(const FieldSet& a, const FieldSet& b, const PublicKey& pk);
/*
 * Verifica el certificado de coprimalidad de dos complementos utilizando
 * e(acc_G1(D), U_G2) * e(acc_G1(E), V_G2) = e(g1, g2).
 *
 * Parametros:
 *   a      testigo de separacion del primer complemento D
 *   b      testigo de separacion del segundo complemento E
 *   proof  testigos de Bezout recibidos en G2
 *
 * Devuelve:
 *   true si los puntos G2 son validos y se cumple la igualdad de pairings;
 *   false si alguna comprobacion falla.
 */
bool verify_disjoint(const SplitWitness& a, const SplitWitness& b,
                     const DisjointWitness& proof);
}  // namespace tt::aso

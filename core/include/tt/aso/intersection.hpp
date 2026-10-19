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
struct SplitWitness {
    bn::Ec1 rest_g1;
    bn::Ec2 rest_g2;
};
struct DisjointWitness {
    bn::Ec2 bezout_u;
    bn::Ec2 bezout_v;
};

SplitWitness prove_split(const FieldSet& complement, const PublicKey& pk);
bool verify_split(const bn::Ec1& authenticated, const FieldSet& intersection_part,
                  const SplitWitness& proof, const PublicKey& pk);
DisjointWitness prove_disjoint(const FieldSet& a, const FieldSet& b, const PublicKey& pk);
bool verify_disjoint(const SplitWitness& a, const SplitWitness& b,
                     const DisjointWitness& proof);
}  // namespace tt::aso

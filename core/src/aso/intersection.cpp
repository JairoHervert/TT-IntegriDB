#include "tt/aso/intersection.hpp"

#include <NTL/ZZ_pX.h>
#include <stdexcept>

#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/pairing_group.hpp"

namespace tt::aso {
namespace {
// Los puntos G2 recibidos del servidor nunca se confian: deben estar en
// el subgrupo de orden primo incluso si su serializacion es correcta.
bool valid_g2(const bn::Ec2& p) { return crypto::in_prime_order_subgroup(p); }
}

SplitWitness prove_split(const FieldSet& complement, const PublicKey& pk) {
    return {accumulate_g1(complement, pk), accumulate_g2(complement, pk)};
}

bool verify_split(const bn::Ec1& authenticated, const FieldSet& intersection_part,
                  const SplitWitness& proof, const PublicKey& pk) {
    const auto& group = crypto::init_pairing_group();
    if (!valid_g2(proof.rest_g2)) return false;
    // El mismo complemento se representa en G1 y G2.
    if (!(crypto::pairing(proof.rest_g1, group.g2) ==
          crypto::pairing(group.g1, proof.rest_g2))) return false;
    // Demostracion de P_C(s)*P_D(s)=P_N(s).
    const bn::Ec1 part = accumulate_g1(intersection_part, pk);
    return crypto::pairing(part, proof.rest_g2) == crypto::pairing(authenticated, group.g2);
}

DisjointWitness prove_disjoint(const FieldSet& a, const FieldSet& b, const PublicKey& pk) {
    crypto::init_pairing_group();
    const NTL::ZZ_pX pa = characteristic_polynomial(a);
    const NTL::ZZ_pX pb = characteristic_polynomial(b);
    NTL::ZZ_pX gcd, u, v;
    NTL::XGCD(gcd, u, v, pa, pb);
    if (!NTL::IsOne(gcd)) throw std::invalid_argument("ASO: los complementos no son disjuntos");
    if (NTL::deg(u) >= static_cast<long>(pk.g2_s_powers.size()) ||
        NTL::deg(v) >= static_cast<long>(pk.g2_s_powers.size())) {
        throw std::length_error("ASO: el testigo de Bezout supera q");
    }
    return {
        crypto::linear_combination(pk.g2_s_powers, 0, u.rep),
        crypto::linear_combination(pk.g2_s_powers, 0, v.rep)
    };
}

bool verify_disjoint(const SplitWitness& a, const SplitWitness& b,
                     const DisjointWitness& proof) {
    const auto& group = crypto::init_pairing_group();
    if (!valid_g2(proof.bezout_u) || !valid_g2(proof.bezout_v)) return false;
    // e(g1^{P_D(s)}, g2^{U(s)}) * e(g1^{P_E(s)}, g2^{V(s)}) = e(g1,g2).
    return crypto::pairing(a.rest_g1, proof.bezout_u) *
               crypto::pairing(b.rest_g1, proof.bezout_v) ==
           crypto::pairing(group.g1, group.g2);
}
}  // namespace tt::aso

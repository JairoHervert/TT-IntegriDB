// accumulator.cpp

#include "tt/aso/accumulator.hpp"

#include <NTL/vec_ZZ_p.h>

#include <stdexcept>
#include <string>

#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/pairing_group.hpp"

namespace tt::aso {

static_assert(sizeof(long) >= sizeof(Element),
              "NTL::conv<ZZ_p>(long) debe poder representar cualquier Element");

namespace {

void check_capacity(std::size_t set_size, const PublicKey& pk) {
    if (pk.g1_s_powers.empty() || pk.g2_s_powers.size() != pk.g1_s_powers.size()) {
        throw std::invalid_argument("aso: clave publica vacia o incompleta");
    }
    if (set_size > pk.q()) {
        throw std::length_error("aso: el conjunto tiene " + std::to_string(set_size) +
                                " elementos y la clave publica solo admite q = " +
                                std::to_string(pk.q()));
    }
}

}  // namespace

NTL::ZZ_p to_field(Element x) {
    crypto::init_pairing_group();
    return NTL::conv<NTL::ZZ_p>(static_cast<long>(x));
}

FieldSet to_field(const std::vector<Element>& set) {
    FieldSet out;
    out.elements.reserve(set.size());
    for (Element x : set) out.elements.push_back(to_field(x));
    return out;
}

NTL::ZZ_pX characteristic_polynomial(const FieldSet& set) {
    crypto::init_pairing_group();
    NTL::vec_ZZ_p roots;
    roots.SetLength(static_cast<long>(set.elements.size()));
    for (std::size_t i = 0; i < set.elements.size(); ++i) {
        roots[static_cast<long>(i)] = -set.elements[i];  // X + x = X - (-x)
    }
    NTL::ZZ_pX poly;
    NTL::BuildFromRoots(poly, roots);  // prod (X - roots[i]); 1 si no hay raices
    return poly;
}

NTL::ZZ_pX characteristic_polynomial(const std::vector<Element>& set) {
    return characteristic_polynomial(to_field(set));
}

NTL::ZZ_p characteristic_value(const FieldSet& set, const NTL::ZZ_p& s) {
    crypto::init_pairing_group();
    NTL::ZZ_p value(1);
    for (const NTL::ZZ_p& x : set.elements) value *= (x + s);
    return value;
}

NTL::ZZ_p characteristic_value(const std::vector<Element>& set, const NTL::ZZ_p& s) {
    return characteristic_value(to_field(set), s);
}

bn::Ec1 accumulate_g1(const FieldSet& set, const PublicKey& pk) {
    check_capacity(set.elements.size(), pk);
    const NTL::ZZ_pX poly = characteristic_polynomial(set);
    return crypto::linear_combination(pk.g1_s_powers, 0, poly.rep);
}

bn::Ec2 accumulate_g2(const FieldSet& set, const PublicKey& pk) {
    check_capacity(set.elements.size(), pk);
    const NTL::ZZ_pX poly = characteristic_polynomial(set);
    return crypto::linear_combination(pk.g2_s_powers, 0, poly.rep);
}

bn::Ec1 accumulate_g1(const std::vector<Element>& set, const PublicKey& pk) {
    return accumulate_g1(to_field(set), pk);
}

bn::Ec2 accumulate_g2(const std::vector<Element>& set, const PublicKey& pk) {
    return accumulate_g2(to_field(set), pk);
}

bn::Ec1 accumulate_g1_with_secret(const FieldSet& set, const SecretKey& sk) {
    const crypto::PairingGroup& group = crypto::init_pairing_group();
    return group.g1 * crypto::to_vuint(characteristic_value(set, sk.s));
}

bn::Ec2 accumulate_g2_with_secret(const FieldSet& set, const SecretKey& sk) {
    const crypto::PairingGroup& group = crypto::init_pairing_group();
    return group.g2 * crypto::to_vuint(characteristic_value(set, sk.s));
}

bn::Ec1 accumulate_g1_with_secret(const std::vector<Element>& set, const SecretKey& sk) {
    return accumulate_g1_with_secret(to_field(set), sk);
}

bn::Ec2 accumulate_g2_with_secret(const std::vector<Element>& set, const SecretKey& sk) {
    return accumulate_g2_with_secret(to_field(set), sk);
}

}  // namespace tt::aso

#include "tt/aso/sum.hpp"

#include <cstdint>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

#include "tt/ait/node.hpp"
#include "tt/aso/accumulator.hpp"
#include "tt/crypto/group_ops.hpp"

namespace tt::aso {
SumWitness prove_sum(const std::vector<std::int64_t>& values, const PublicKey& pk) {
    crypto::init_pairing_group();
    if (values.empty() || values.size() > pk.q()) throw std::invalid_argument("SUM: numero de valores no soportado");
    FieldSet set;
    set.elements.reserve(values.size());
    for (auto v : values) {
        if (v < 5 || v > 10) throw std::invalid_argument("C10: grade debe estar entre 5 y 10");
        set.elements.push_back(ait::value_to_element(v));
    }
    const NTL::ZZ_pX p = characteristic_polynomial(set);
    const auto n = static_cast<std::size_t>(NTL::deg(p));
    std::vector<NTL::ZZ_p> c1, c2;
    for (std::size_t i = 1; i <= n; ++i) c1.push_back(NTL::coeff(p, static_cast<long>(i)));
    for (std::size_t i = 2; i <= n; ++i) c2.push_back(NTL::coeff(p, static_cast<long>(i)));
    const auto a0 = crypto::scalar_to_bytes(NTL::coeff(p, 0));
    const auto a1 = crypto::scalar_to_bytes(NTL::coeff(p, 1));
    SumWitness result;
    std::copy(a0.begin(), a0.end(), result.a0.begin());
    std::copy(a1.begin(), a1.end(), result.a1.begin());
    result.w1 = crypto::linear_combination(pk.g1_s_powers, 0, c1);
    result.w2 = crypto::linear_combination(pk.g1_s_powers, 0, c2);
    return result;
}

bool verify_sum(const bn::Ec1& acc, std::uint64_t count, std::int64_t sum,
                const SumWitness& witness, const PublicKey& pk) {
    const auto& group = crypto::init_pairing_group();
    if (count == 0 || count > pk.q() || sum < 0 ||
        count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 10)) return false;
    // Evita aceptar una suma modular que no sea la suma SQL exacta.
    if (sum < static_cast<std::int64_t>(count) * 5 ||
        sum > static_cast<std::int64_t>(count) * 10) return false;
    if (pk.g2_s_powers.size() < 2 || pk.g1_s_powers.size() < 2) return false;
    if (!acc.isValid() || (!witness.w1.isZero() && !witness.w1.isValid()) ||
        (!witness.w2.isZero() && !witness.w2.isValid())) return false;
    try {
        const auto a0 = crypto::scalar_from_bytes(std::vector<unsigned char>(witness.a0.begin(), witness.a0.end()));
        const auto a1 = crypto::scalar_from_bytes(std::vector<unsigned char>(witness.a1.begin(), witness.a1.end()));
        if (NTL::IsZero(a0) || a1 != a0 * to_field(sum)) return false;
        // Ecuaciones equivalentes a las del paper, escritas sin restar
        // puntos de G1. El punto g2^s es publico, el secreto s no se usa.
        const bn::Ec1 a0_g1 = group.g1 * crypto::to_vuint(a0);
        const bn::Ec1 a1_g1 = group.g1 * crypto::to_vuint(a1);
        return crypto::pairing(witness.w1, pk.g2_s_powers[1]) *
                   crypto::pairing(a0_g1, group.g2) == crypto::pairing(acc, group.g2) &&
               crypto::pairing(witness.w2, pk.g2_s_powers[1]) *
                   crypto::pairing(a1_g1, group.g2) == crypto::pairing(witness.w1, group.g2);
    } catch (const std::exception&) {
        return false;
    }
}
}  // namespace tt::aso

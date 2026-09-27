// keys.cpp

#include "tt/aso/keys.hpp"

#include <NTL/ZZ.h>

#include <stdexcept>
#include <string>

#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/crypto/random.hpp"

namespace tt::aso {

namespace {

// Coeficiente aleatorio de 128 bits distinto de cero para las
// comprobaciones por lotes. 128 bits bastan para que la probabilidad de
// aceptar una clave mal formada sea ~2^-128, y hacen las multiplicaciones
// escalares cerca del doble de rapidas que con escalares de 254 bits.
NTL::ZZ_p random_challenge() {
    while (true) {
        std::vector<unsigned char> buf = crypto::random_bytes(16);
        NTL::ZZ value;
        NTL::ZZFromBytes(value, buf.data(), static_cast<long>(buf.size()));
        NTL::ZZ_p c = NTL::conv<NTL::ZZ_p>(value);
        if (!NTL::IsZero(c)) return c;
    }
}

template <class Point>
bool all_valid_nonzero(const std::vector<Point>& points) {
    for (const Point& p : points) {
        if (p.isZero() || !p.isValid()) return false;
    }
    return true;
}

}  // namespace

KeyPair init(std::size_t q) {
    if (q == 0 || q > kMaxQ) {
        throw std::invalid_argument("aso::init: q debe estar entre 1 y " + std::to_string(kMaxQ));
    }
    const crypto::PairingGroup& group = crypto::init_pairing_group();

    KeyPair kp;
    kp.sk.s = crypto::random_nonzero_scalar();
    kp.sk.a = crypto::random_nonzero_scalar();

    PublicKey& pk = kp.pk;
    pk.g1_s_powers.resize(q + 1);
    pk.g2_s_powers.resize(q + 1);
    pk.g1_as_powers.resize(q + 1);

    NTL::ZZ_p s_pow(1);  // s^i, empezando en s^0 = 1
    for (std::size_t i = 0; i <= q; ++i) {
        const mie::Vuint e = crypto::to_vuint(s_pow);
        pk.g1_s_powers[i] = group.g1 * e;
        pk.g2_s_powers[i] = group.g2 * e;
        pk.g1_as_powers[i] = group.g1 * crypto::to_vuint(kp.sk.a * s_pow);
        s_pow *= kp.sk.s;
    }
    pk.g2_a = group.g2 * crypto::to_vuint(kp.sk.a);
    return kp;
}

bool is_well_formed(const PublicKey& pk) {
    const crypto::PairingGroup& group = crypto::init_pairing_group();

    // Tamanos
    const std::size_t n = pk.g1_s_powers.size();  // q + 1
    if (n < 2 || n - 1 > kMaxQ || pk.g2_s_powers.size() != n || pk.g1_as_powers.size() != n) {
        return false;
    }

    // Puntos en la curva, distintos de O, y los de G2 en el subgrupo de orden r
    if (!all_valid_nonzero(pk.g1_s_powers) || !all_valid_nonzero(pk.g2_s_powers) ||
        !all_valid_nonzero(pk.g1_as_powers) || pk.g2_a.isZero() || !pk.g2_a.isValid()) {
        return false;
    }
    for (const bn::Ec2& p : pk.g2_s_powers) {
        if (!crypto::in_prime_order_subgroup(p)) return false;
    }
    if (!crypto::in_prime_order_subgroup(pk.g2_a)) return false;

    // La potencia 0 son los generadores
    if (!(pk.g1_s_powers[0] == group.g1) || !(pk.g2_s_powers[0] == group.g2)) return false;

    // Relaciones entre potencias, por lotes. Con rho_i aleatorios:
    //   X  = sum rho_i g1^{s^i}          (i = 0..q)
    //   Y  = sum rho_i g2^{s^i}          (i = 0..q)
    //   Z  = sum rho_i g1^{a s^i}        (i = 0..q)
    //   Xn = sum rho_i g1^{s^{i+1}}      (i = 0..q-1)
    //   Xp = sum rho_i g1^{s^i}          (i = 0..q-1)
    // y se comprueba:
    //   e(X, g2)  = e(g1, Y)       -> las potencias en G1 y G2 coinciden
    //   e(Xn, g2) = e(Xp, g2^s)    -> cada potencia es la anterior por s
    //   e(Z, g2)  = e(X, g2^a)     -> la parte de extractabilidad usa el mismo a
    std::vector<NTL::ZZ_p> rho(n);
    for (NTL::ZZ_p& r : rho) r = random_challenge();

    const bn::Ec1 x = crypto::linear_combination(pk.g1_s_powers, 0, rho);
    const bn::Ec2 y = crypto::linear_combination(pk.g2_s_powers, 0, rho);
    const bn::Ec1 z = crypto::linear_combination(pk.g1_as_powers, 0, rho);
    const bn::Ec1 x_next = crypto::linear_combination(pk.g1_s_powers, 1, rho.data(), n - 1);
    const bn::Ec1 x_prev = crypto::linear_combination(pk.g1_s_powers, 0, rho.data(), n - 1);

    using crypto::pairing;
    if (!(pairing(x, group.g2) == pairing(group.g1, y))) return false;
    if (!(pairing(x_next, group.g2) == pairing(x_prev, pk.g2_s_powers[1]))) return false;
    if (!(pairing(z, group.g2) == pairing(x, pk.g2_a))) return false;
    return true;
}

}  // namespace tt::aso

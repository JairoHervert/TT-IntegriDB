// pairing_group_test.cpp
//
// Pruebas de tt::crypto: grupo bilineal y aleatoriedad.

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>

#include <sstream>
#include <string>
#include <thread>

#include "test_support.hpp"
#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/crypto/random.hpp"

int main() {
    using tt::crypto::pairing;
    using tt::crypto::to_vuint;
    using tt::test::check;
    using tt::test::section;

    const tt::crypto::PairingGroup& group = tt::crypto::init_pairing_group();

    section("Parametros");
    check(group.g1.isValid(), "g1 esta en la curva");
    check(group.g2.isValid(), "g2 esta en la curva torcida");
    check((group.g1 * bn::Param::r).isZero(), "g1 * r = O");
    check(tt::crypto::in_prime_order_subgroup(group.g2), "g2 * r = O");
    {
        std::ostringstream os;
        os << NTL::ZZ_p::modulus();
        check(os.str() == tt::crypto::group_order_decimal(), "modulo de NTL == orden r");
    }
    check(&tt::crypto::init_pairing_group() == &group,
          "llamar init dos veces devuelve el mismo grupo");
    {
        // NTL guarda el modulo por hilo: en un hilo nuevo tambien debe quedar fijado.
        bool ok = false;
        std::thread worker([&ok] {
            tt::crypto::init_pairing_group();
            std::ostringstream os;
            os << NTL::ZZ_p::modulus();
            ok = os.str() == tt::crypto::group_order_decimal();
        });
        worker.join();
        check(ok, "en otro hilo, init tambien fija el modulo de NTL");
    }

    section("Emparejamiento");
    {
        const NTL::ZZ_p a = tt::crypto::random_nonzero_scalar();
        const NTL::ZZ_p b = tt::crypto::random_nonzero_scalar();
        const bn::Fp12 base = pairing(group.g1, group.g2);
        check(!(base == bn::Fp12(1)), "e(g1, g2) != 1 (no degenerado)");
        check(pairing(group.g1 * to_vuint(a), group.g2 * to_vuint(b)) ==
                  mie::power(base, to_vuint(a * b)),
              "e(g1*a, g2*b) == e(g1, g2)^(a*b)");
        check(pairing(group.g1 * to_vuint(a), group.g2) == pairing(group.g1, group.g2 * to_vuint(a)),
              "e(g1*a, g2) == e(g1, g2*a)");
    }

    section("Aleatoriedad");
    {
        const NTL::ZZ_p x = tt::crypto::random_nonzero_scalar();
        const NTL::ZZ_p y = tt::crypto::random_nonzero_scalar();
        check(!NTL::IsZero(x) && !NTL::IsZero(y), "los escalares aleatorios no son cero");
        check(x != y, "dos escalares aleatorios son distintos");
        check(tt::crypto::random_bytes(32) != tt::crypto::random_bytes(32),
              "dos bloques de bytes aleatorios son distintos");
    }

    section("Combinacion lineal");
    {
        // Escalares como mie::Vuint (sin signo): con int, ate-pairing hace un
        // corrimiento de bits con signo que es comportamiento indefinido.
        const auto times = [&](unsigned k) { return group.g1 * mie::Vuint(k); };
        const std::vector<bn::Ec1> bases = {group.g1, times(2), times(3)};
        std::vector<NTL::ZZ_p> scalars = {NTL::ZZ_p(5), NTL::ZZ_p(0), NTL::ZZ_p(1)};
        // 5*1 + 0*2 + 1*3 = 8
        check(tt::crypto::linear_combination(bases, 0, scalars) == times(8),
              "5*g1 + 0*(2 g1) + 1*(3 g1) = 8 g1");
        check(tt::crypto::linear_combination(bases, 1, scalars.data(), 2) == times(10),
              "desde la base 1: 5*(2 g1) + 0*(3 g1) = 10 g1");
        tt::test::check_throws<std::out_of_range>(
            [&] { tt::crypto::linear_combination(bases, 2, scalars); },
            "mas escalares que bases se rechaza");
        check(tt::crypto::linear_combination(bases, 0, std::vector<NTL::ZZ_p>{}).isZero(),
              "sin escalares da el punto al infinito");
    }

    return tt::test::finish();
}

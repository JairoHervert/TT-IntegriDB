// pairing_group_test.cpp
//
// Pruebas de tt::crypto (grupo bilineal). Son las secciones 1 y 2 de
// TT_auxiliares/pairing-smoke-test, ahora usando el nucleo en lugar de
// llamar a ate-pairing directamente.
//
// Se ejecuta con:  ctest --test-dir build --output-on-failure

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>

#include <cstdio>
#include <sstream>
#include <string>

#include "tt/crypto/pairing_group.hpp"

namespace {

int failures = 0;

void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FALLO", name);
    if (!ok) ++failures;
}

}  // namespace

int main() {
    using tt::crypto::pairing;
    using tt::crypto::to_vuint;

    const tt::crypto::PairingGroup& group = tt::crypto::init_pairing_group();

    std::printf("Parametros\n");
    check("g1 esta en la curva", group.g1.isValid());
    check("g2 esta en la curva torcida", group.g2.isValid());
    check("g1 * r = O", (group.g1 * bn::Param::r).isZero());
    check("g2 * r = O", (group.g2 * bn::Param::r).isZero());
    {
        std::ostringstream os;
        os << NTL::ZZ_p::modulus();
        check("modulo de NTL == orden r", os.str() == tt::crypto::group_order_decimal());
    }
    check("llamar init dos veces devuelve el mismo grupo",
          &tt::crypto::init_pairing_group() == &group);

    std::printf("Emparejamiento\n");
    {
        NTL::ZZ_p a, b;
        NTL::random(a);
        NTL::random(b);
        const bn::Fp12 base = pairing(group.g1, group.g2);
        check("e(g1, g2) != 1 (no degenerado)", !(base == bn::Fp12(1)));
        check("e(g1*a, g2*b) == e(g1, g2)^(a*b)",
              pairing(group.g1 * to_vuint(a), group.g2 * to_vuint(b)) ==
                  mie::power(base, to_vuint(a * b)));
        check("e(g1*a, g2) == e(g1, g2*a)",
              pairing(group.g1 * to_vuint(a), group.g2) ==
                  pairing(group.g1, group.g2 * to_vuint(a)));
    }

    std::printf("%s (%d fallas)\n", failures == 0 ? "TODO BIEN" : "HAY FALLAS", failures);
    return failures == 0 ? 0 : 1;
}

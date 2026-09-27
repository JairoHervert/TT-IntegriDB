// aso_test.cpp
//
// Pruebas del bloque ASO: claves (init, is_well_formed) y acumulador.

#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pX.h>

#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#include "test_support.hpp"
#include "tt/aso/accumulator.hpp"
#include "tt/aso/keys.hpp"
#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/pairing_group.hpp"

int main() {
    using namespace tt;
    using aso::Element;
    using crypto::pairing;
    using crypto::to_vuint;
    using test::check;
    using test::check_throws;
    using test::section;

    const crypto::PairingGroup& group = crypto::init_pairing_group();
    const std::size_t q = 16;
    const aso::KeyPair keys = aso::init(q);
    const aso::PublicKey& pk = keys.pk;
    const aso::SecretKey& sk = keys.sk;

    section("Init");
    check(pk.q() == q, "pk.q() == q");
    check(pk.g1_s_powers.size() == q + 1 && pk.g2_s_powers.size() == q + 1 &&
              pk.g1_as_powers.size() == q + 1,
          "las tres listas tienen q + 1 potencias");
    check(pk.g1_s_powers[0] == group.g1 && pk.g2_s_powers[0] == group.g2,
          "la potencia 0 son los generadores");
    check(pk.g1_s_powers[1] == group.g1 * to_vuint(sk.s), "g1_s_powers[1] = g1^s");
    check(pk.g2_s_powers[q] == group.g2 * to_vuint(NTL::power(sk.s, static_cast<long>(q))),
          "g2_s_powers[q] = g2^{s^q}");
    check(pk.g1_as_powers[3] == group.g1 * to_vuint(sk.a * NTL::power(sk.s, 3L)),
          "g1_as_powers[3] = g1^{a s^3}");
    check(pk.g2_a == group.g2 * to_vuint(sk.a), "g2_a = g2^a");
    check(!NTL::IsZero(sk.s) && !NTL::IsZero(sk.a), "s y a no son cero");

    const aso::KeyPair other = aso::init(q);
    check(other.sk.s != sk.s && other.sk.a != sk.a, "dos ejecuciones de init dan secretos distintos");
    check_throws<std::invalid_argument>([] { aso::init(0); }, "init con q = 0 se rechaza");
    check_throws<std::invalid_argument>([] { aso::init(aso::kMaxQ + 1); },
                                        "init con q > kMaxQ se rechaza");

    section("Estructura de la clave publica (is_well_formed)");
    check(aso::is_well_formed(pk), "la clave que produce init es valida");
    {
        aso::PublicKey bad = pk;
        bad.g1_s_powers[5] = bad.g1_s_powers[5] + group.g1;
        check(!aso::is_well_formed(bad), "detecta una potencia de G1 alterada");
    }
    {
        aso::PublicKey bad = pk;
        bad.g2_s_powers[7] = bad.g2_s_powers[7] + group.g2;
        check(!aso::is_well_formed(bad), "detecta una potencia de G2 alterada");
    }
    {
        aso::PublicKey bad = pk;
        bad.g1_as_powers[q] = bad.g1_as_powers[q] + group.g1;
        check(!aso::is_well_formed(bad), "detecta una potencia de extractabilidad alterada");
    }
    {
        aso::PublicKey bad = pk;
        bad.g2_a = bad.g2_a + group.g2;
        check(!aso::is_well_formed(bad), "detecta g2^a alterado");
    }
    {
        aso::PublicKey bad = pk;
        std::swap(bad.g1_s_powers[2], bad.g1_s_powers[3]);
        std::swap(bad.g2_s_powers[2], bad.g2_s_powers[3]);
        std::swap(bad.g1_as_powers[2], bad.g1_as_powers[3]);
        check(!aso::is_well_formed(bad), "detecta potencias en desorden");
    }
    {
        aso::PublicKey bad = pk;
        bad.g1_as_powers = other.pk.g1_as_powers;
        bad.g2_a = other.pk.g2_a;
        check(!aso::is_well_formed(bad), "detecta una clave armada con partes de dos claves");
    }
    {
        aso::PublicKey bad = pk;
        bad.g1_s_powers.pop_back();
        check(!aso::is_well_formed(bad), "detecta listas de distinto tamano");
    }

    section("Acumulador");
    const std::vector<Element> set = {3, 7, 11, 20};
    const bn::Ec1 acc = aso::accumulate_g1(set, pk);
    check(acc == aso::accumulate_g1_with_secret(set, sk),
          "G1: el probador (solo pk) y el propietario (con s) obtienen lo mismo");
    check(aso::accumulate_g2(set, pk) == aso::accumulate_g2_with_secret(set, sk),
          "G2: el probador (solo pk) y el propietario (con s) obtienen lo mismo");
    check(aso::accumulate_g1({20, 3, 11, 7}, pk) == acc, "el orden de los elementos no importa");
    check(!(aso::accumulate_g1({3, 7, 11, 21}, pk) == acc), "cambiar un elemento cambia el acumulador");
    check(!(aso::accumulate_g1({3, 7, 11}, pk) == acc), "quitar un elemento cambia el acumulador");
    {
        const NTL::ZZ_p three_plus_s = aso::to_field(3) + sk.s;
        check(aso::accumulate_g1({3, 3}, pk) == group.g1 * to_vuint(three_plus_s * three_plus_s) &&
                  !(aso::accumulate_g1({3, 3}, pk) == aso::accumulate_g1({3}, pk)),
              "multiconjunto: {3, 3} acumula (3 + s)^2 y es distinto de {3}");
    }
    {
        const std::vector<Element> empty;
        check(aso::accumulate_g1(empty, pk) == group.g1 && aso::accumulate_g2(empty, pk) == group.g2 &&
                  aso::accumulate_g1_with_secret(empty, sk) == group.g1 &&
                  aso::accumulate_g1(aso::FieldSet{}, pk) == group.g1,
              "acc(conjunto vacio) = g (el producto vacio es 1)");
    }
    {
        const std::vector<Element> extremes = {-5, 0, std::numeric_limits<Element>::max(),
                                               std::numeric_limits<Element>::min()};
        check(aso::accumulate_g1(extremes, pk) == aso::accumulate_g1_with_secret(extremes, sk),
              "negativos, cero y extremos de int64 coinciden en ambas formas");
    }
    {
        std::vector<Element> full(q);
        std::iota(full.begin(), full.end(), Element{1});
        check(aso::accumulate_g1(full, pk) == aso::accumulate_g1_with_secret(full, sk),
              "conjunto de tamano exactamente q");
        full.push_back(Element{1000});
        check_throws<std::length_error>([&] { aso::accumulate_g1(full, pk); },
                                        "conjunto de tamano q + 1 se rechaza (G1)");
        check_throws<std::length_error>([&] { aso::accumulate_g2(full, pk); },
                                        "conjunto de tamano q + 1 se rechaza (G2)");
    }
    {
        const NTL::ZZ_pX poly = aso::characteristic_polynomial(set);
        check(NTL::deg(poly) == 4 && NTL::IsOne(NTL::LeadCoeff(poly)),
              "P_S es monico y de grado |S|");
        check(NTL::eval(poly, sk.s) == aso::characteristic_value(set, sk.s),
              "P_S(s) coincide calculado de dos formas");
        check(NTL::IsOne(aso::characteristic_polynomial(aso::FieldSet{})), "P_vacio = 1");
        const aso::FieldSet inverses{{NTL::inv(aso::to_field(4)), NTL::inv(aso::to_field(9))}};
        check(aso::accumulate_g1(inverses, pk) == aso::accumulate_g1_with_secret(inverses, sk),
              "tambien funciona con elementos de Z_r (inversos, como los usa el AIT)");
    }

    section("Relaciones con el emparejamiento");
    check(pairing(acc, group.g2) == pairing(group.g1, aso::accumulate_g2(set, pk)),
          "e(acc_G1(S), g2) = e(g1, acc_G2(S))");
    {
        // Testigo de pertenencia de 7 en S: W = acc(S \ {7}) y
        // e(W, g2^{s + 7}) = e(acc(S), g2). Es la idea que usa el ASO para
        // probar subconjuntos e intersecciones.
        const bn::Ec1 witness = aso::accumulate_g1({3, 11, 20}, pk);
        const bn::Ec2 g2_s_plus_7 = pk.g2_s_powers[1] + group.g2 * to_vuint(aso::to_field(7));
        const bn::Ec2 g2_s_plus_8 = pk.g2_s_powers[1] + group.g2 * to_vuint(aso::to_field(8));
        check(pairing(witness, g2_s_plus_7) == pairing(acc, group.g2),
              "el testigo de pertenencia de 7 en S se verifica");
        check(!(pairing(witness, g2_s_plus_8) == pairing(acc, group.g2)),
              "el mismo testigo no sirve para 8, que no esta en S");
    }
    {
        // Extractabilidad: g1^{a P(s)} calculado con g1_as_powers cumple
        // e(g1^{a P(s)}, g2) = e(g1^{P(s)}, g2^a).
        const NTL::ZZ_pX poly = aso::characteristic_polynomial(set);
        const bn::Ec1 acc_a = crypto::linear_combination(pk.g1_as_powers, 0, poly.rep);
        check(pairing(acc_a, group.g2) == pairing(acc, pk.g2_a),
              "e(g1^{a P(s)}, g2) = e(g1^{P(s)}, g2^a)");
    }

    return test::finish();
}

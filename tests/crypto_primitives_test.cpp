// crypto_primitives_test.cpp
//
// Pruebas de SHA-256 con codificacion canonica, de Enc (AES-128-CBC con IV
// sintetico) y de la conversion de escalares a bytes.

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/crypto/random.hpp"
#include "tt/crypto/symmetric.hpp"

/*
 * Pruebas de las primitivas criptograficas: SHA-256 con codificacion
 * canonica (vectores conocidos, separacion de campos), Enc (AES-128-CBC con
 * IV sintetico: determinismo, descifrado y deteccion de alteraciones) y la
 * conversion de escalares a 32 bytes y de regreso.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    using namespace tt;
    using test::check;
    using test::check_throws;
    using test::section;

    const crypto::PairingGroup& group = crypto::init_pairing_group();

    section("SHA-256 y codificacion canonica");
    {
        crypto::Sha256 h;
        h.bytes("abc", 3);
        check(crypto::to_hex(h.finish()) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256(\"abc\") coincide con el vector de prueba de FIPS 180-2");
    }
    {
        const unsigned char one[8] = {0, 0, 0, 0, 0, 0, 0, 1};
        crypto::Sha256 a, b;
        a.u64(1);
        b.bytes(one, 8);
        check(a.finish() == b.finish(), "u64(1) se codifica como 8 bytes big-endian");

        const unsigned char minus_one[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        crypto::Sha256 c, d;
        c.i64(-1);
        d.bytes(minus_one, 8);
        check(c.finish() == d.finish(), "i64(-1) se codifica en complemento a dos");

        crypto::Sha256 e, f;
        e.length_prefixed({'a', 'b'}).length_prefixed({'c'});
        f.length_prefixed({'a'}).length_prefixed({'b', 'c'});
        check(!(e.finish() == f.finish()), "con prefijo de longitud, (ab, c) y (a, bc) dan hashes distintos");
    }
    {
        const bn::Ec1 p1 = group.g1 * mie::Vuint(3);
        const bn::Ec1 p2 = group.g1 + group.g1 + group.g1;  // otras coordenadas proyectivas
        check(crypto::encode_point(p1) == crypto::encode_point(p2),
              "el mismo punto con distinta representacion interna se codifica igual");
        const bn::Ec2 q1 = group.g2 * mie::Vuint(5);
        const bn::Ec2 q2 = group.g2 + group.g2 + group.g2 + group.g2 + group.g2;
        check(crypto::encode_point(q1) == crypto::encode_point(q2), "lo mismo en G2");
        check(!(crypto::encode_point(p1) == crypto::encode_point(group.g1)),
              "puntos distintos se codifican distinto");

        const auto zero = crypto::encode_point(crypto::zero_point<bn::Ec1>());
        bool all_zero = true;
        for (unsigned char b : zero) all_zero = all_zero && b == 0;
        const auto g = crypto::encode_point(group.g1);
        check(all_zero && g[0] == 1, "el punto al infinito tiene marca 0 y un punto afin marca 1");
    }

    section("Enc: AES-128-CBC con IV sintetico");
    {
        crypto::AesKey key{};
        const std::vector<unsigned char> kb = crypto::random_bytes(key.size());
        std::copy(kb.begin(), kb.end(), key.begin());
        crypto::AesKey other = key;
        other[0] ^= 0x80;

        const std::vector<unsigned char> msg = crypto::random_bytes(32);
        const std::vector<unsigned char> sealed = crypto::encrypt_deterministic(key, msg);
        check(sealed.size() == 16 + 48, "32 bytes cifrados ocupan IV (16) + 3 bloques (48)");
        check(crypto::decrypt_deterministic(key, sealed) == msg, "descifrar devuelve el mensaje original");
        check(crypto::encrypt_deterministic(key, msg) == sealed,
              "mismo mensaje y misma clave: mismo cifrado (determinista)");
        std::vector<unsigned char> msg2 = msg;
        msg2[31] ^= 0x01;
        check(!(crypto::encrypt_deterministic(key, msg2) == sealed), "otro mensaje: otro cifrado");
        check(!(crypto::encrypt_deterministic(other, msg) == sealed), "otra clave: otro cifrado");
        check(crypto::decrypt_deterministic(key, crypto::encrypt_deterministic(key, {})).empty(),
              "tambien cifra un mensaje vacio");

        bool every_byte_detected = true;
        for (std::size_t i = 0; i < sealed.size(); ++i) {
            std::vector<unsigned char> bad = sealed;
            bad[i] ^= 0x01;
            try {
                crypto::decrypt_deterministic(key, bad);
                every_byte_detected = false;
            } catch (const crypto::DecryptionError&) {
            }
        }
        check(every_byte_detected, "alterar cualquiera de los 64 bytes (IV o cifrado) se detecta");
        check_throws<crypto::DecryptionError>([&] { crypto::decrypt_deterministic(other, sealed); },
                                              "descifrar con otra clave se detecta");
        check_throws<crypto::DecryptionError>(
            [&] { crypto::decrypt_deterministic(key, std::vector<unsigned char>(sealed.begin(), sealed.end() - 1)); },
            "una longitud invalida se rechaza");
    }

    section("Escalares como bytes");
    {
        const NTL::ZZ_p x = crypto::random_nonzero_scalar();
        const std::vector<unsigned char> bytes = crypto::scalar_to_bytes(x);
        check(bytes.size() == 32 && crypto::scalar_from_bytes(bytes) == x, "ida y vuelta de un escalar");
        check(crypto::scalar_to_bytes(NTL::ZZ_p(1)).back() == 1 && crypto::scalar_to_bytes(NTL::ZZ_p(1))[0] == 0,
              "big-endian: el 1 queda en el ultimo byte");

        std::vector<unsigned char> r(32);
        NTL::BytesFromZZ(r.data(), NTL::conv<NTL::ZZ>(crypto::group_order_decimal()), 32);
        std::reverse(r.begin(), r.end());
        check_throws<std::invalid_argument>([&] { crypto::scalar_from_bytes(r); },
                                            "el valor r (fuera de Z_r) se rechaza");
        check_throws<std::invalid_argument>([&] { crypto::scalar_from_bytes(std::vector<unsigned char>(31)); },
                                            "un tamano distinto de 32 bytes se rechaza");
    }

    return test::finish();
}

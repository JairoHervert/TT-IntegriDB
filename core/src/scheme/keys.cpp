// keys.cpp

#include "tt/scheme/keys.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tt/crypto/pairing_group.hpp"
#include "tt/crypto/random.hpp"

namespace tt::scheme {

/*
 * Implementacion: llama a aso::init, mueve sus claves al par del esquema y
 * copia 16 bytes aleatorios como clave AES. El buffer temporal de la clave
 * se borra de memoria.
 */
KeyPair init(std::size_t q) {
    aso::KeyPair aso_keys = aso::init(q);

    KeyPair kp;
    kp.sk.aso = std::move(aso_keys.sk);
    kp.pk.aso = std::move(aso_keys.pk);

    std::vector<unsigned char> key = crypto::random_bytes(kEncKeyBytes);
    std::copy(key.begin(), key.end(), kp.sk.enc_key.begin());
    OPENSSL_cleanse(key.data(), key.size());
    return kp;
}

/*
 * Implementacion: SHA-256 con la etiqueta 0x21, q y despues cada lista de
 * potencias completa y g2^a, en codificacion canonica. Las tres listas
 * tienen q + 1 puntos, asi que q basta para delimitarlas.
 */
crypto::Digest public_key_fingerprint(const PublicKey& pk) {
    const aso::PublicKey& k = pk.aso;
    if (k.g1_s_powers.size() < 2 || k.g2_s_powers.size() != k.g1_s_powers.size() ||
        k.g1_as_powers.size() != k.g1_s_powers.size()) {
        throw std::invalid_argument("huella: la clave publica esta incompleta");
    }
    crypto::Sha256 h;
    h.u8(0x21).u64(k.q());
    for (const bn::Ec1& p : k.g1_s_powers) h.point(p);
    for (const bn::Ec2& p : k.g2_s_powers) h.point(p);
    for (const bn::Ec1& p : k.g1_as_powers) h.point(p);
    h.point(k.g2_a);
    return h.finish();
}

/*
 * Implementacion: recalcula g1^s, g2^s y g2^a con los secretos y los compara
 * con los de pk.
 */
bool keys_match(const SecretKey& sk, const PublicKey& pk) {
    const crypto::PairingGroup& group = crypto::init_pairing_group();
    const aso::PublicKey& k = pk.aso;
    if (k.g1_s_powers.size() < 2 || k.g2_s_powers.size() < 2) return false;
    const mie::Vuint s = crypto::to_vuint(sk.aso.s);
    const mie::Vuint a = crypto::to_vuint(sk.aso.a);
    return k.g1_s_powers[1] == group.g1 * s && k.g2_s_powers[1] == group.g2 * s && k.g2_a == group.g2 * a;
}

}  // namespace tt::scheme

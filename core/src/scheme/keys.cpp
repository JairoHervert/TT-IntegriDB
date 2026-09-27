// keys.cpp

#include "tt/scheme/keys.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "tt/crypto/random.hpp"

namespace tt::scheme {

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

}  // namespace tt::scheme

// hash.cpp

#include "tt/crypto/hash.hpp"

#include <openssl/evp.h>

#include <stdexcept>
#include <string>

namespace tt::crypto {

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::logic_error("hash: digito hexadecimal inesperado");
}

// Escribe un elemento de F_p como 32 bytes big-endian. Se pasa por la
// representacion hexadecimal que ofrece ate-pairing ("0x...") para no
// depender de su representacion interna.
void write_coordinate(const bn::Fp& x, unsigned char* out) {
    std::string hex = x.toString(16);
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex.erase(0, 2);
    // Quitar ceros a la izquierda sobrantes y rellenar a 64 digitos.
    const std::size_t first = hex.find_first_not_of('0');
    hex = (first == std::string::npos) ? std::string() : hex.substr(first);
    if (hex.size() > 64) throw std::logic_error("hash: coordenada de mas de 256 bits");
    hex.insert(0, 64 - hex.size(), '0');
    for (std::size_t i = 0; i < 32; ++i) {
        out[i] = static_cast<unsigned char>((hex_digit(hex[2 * i]) << 4) | hex_digit(hex[2 * i + 1]));
    }
}

}  // namespace

std::array<unsigned char, kG1EncodedBytes> encode_point(const bn::Ec1& p) {
    std::array<unsigned char, kG1EncodedBytes> out{};
    if (p.isZero()) return out;  // marca 0 y coordenadas en cero
    p.normalize();
    out[0] = 1;
    write_coordinate(p.p[0], out.data() + 1);
    write_coordinate(p.p[1], out.data() + 33);
    return out;
}

std::array<unsigned char, kG2EncodedBytes> encode_point(const bn::Ec2& p) {
    std::array<unsigned char, kG2EncodedBytes> out{};
    if (p.isZero()) return out;
    p.normalize();
    out[0] = 1;
    write_coordinate(p.p[0].a_, out.data() + 1);
    write_coordinate(p.p[0].b_, out.data() + 33);
    write_coordinate(p.p[1].a_, out.data() + 65);
    write_coordinate(p.p[1].b_, out.data() + 97);
    return out;
}

Sha256::Sha256() : ctx_(EVP_MD_CTX_new()) {
    if (ctx_ == nullptr || EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx_);
        throw std::runtime_error("SHA-256: no se pudo inicializar");
    }
}

Sha256::~Sha256() { EVP_MD_CTX_free(ctx_); }

Sha256& Sha256::bytes(const void* data, std::size_t size) {
    if (finished_) throw std::logic_error("SHA-256: se agregaron datos despues de finish()");
    if (size > 0 && EVP_DigestUpdate(ctx_, data, size) != 1) {
        throw std::runtime_error("SHA-256: fallo EVP_DigestUpdate");
    }
    return *this;
}

Sha256& Sha256::u8(std::uint8_t v) { return bytes(&v, 1); }

Sha256& Sha256::u64(std::uint64_t v) {
    unsigned char buf[8];
    for (int i = 7; i >= 0; --i) {
        buf[i] = static_cast<unsigned char>(v & 0xff);
        v >>= 8;
    }
    return bytes(buf, sizeof buf);
}

Sha256& Sha256::i64(std::int64_t v) { return u64(static_cast<std::uint64_t>(v)); }

Sha256& Sha256::digest(const Digest& d) { return bytes(d.data(), d.size()); }

Sha256& Sha256::point(const bn::Ec1& p) {
    const auto enc = encode_point(p);
    return bytes(enc.data(), enc.size());
}

Sha256& Sha256::point(const bn::Ec2& p) {
    const auto enc = encode_point(p);
    return bytes(enc.data(), enc.size());
}

Sha256& Sha256::length_prefixed(const std::vector<unsigned char>& data) {
    u64(data.size());
    return bytes(data.data(), data.size());
}

Digest Sha256::finish() {
    if (finished_) throw std::logic_error("SHA-256: finish() llamado dos veces");
    Digest out{};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(ctx_, out.data(), &size) != 1 || size != out.size()) {
        throw std::runtime_error("SHA-256: fallo EVP_DigestFinal_ex");
    }
    finished_ = true;
    return out;
}

std::string to_hex(const Digest& d) {
    static const char* const kDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(2 * d.size());
    for (unsigned char b : d) {
        hex.push_back(kDigits[b >> 4]);
        hex.push_back(kDigits[b & 0x0f]);
    }
    return hex;
}

}  // namespace tt::crypto

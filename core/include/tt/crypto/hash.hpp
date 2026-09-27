// hash.hpp
//
// SHA-256 (la funcion H del reporte) con codificacion canonica de los datos
// que se hashean.
//
// Para que un hash de Merkle sea seguro, cada dato debe tener una sola
// codificacion en bytes y dos secuencias distintas de campos nunca deben dar
// los mismos bytes. Por eso: enteros de 8 bytes big-endian, puntos en
// coordenadas afines de 32 bytes por coordenada con un byte de marca, y
// cadenas de bytes con su longitud por delante.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bn.h"

typedef struct evp_md_ctx_st EVP_MD_CTX;  // de OpenSSL, sin incluir sus cabeceras aqui

namespace tt::crypto {

constexpr std::size_t kDigestBytes = 32;
using Digest = std::array<unsigned char, kDigestBytes>;

// Tamanos de la codificacion canonica: 1 byte de marca (0 = punto al
// infinito, 1 = punto afin) + las coordenadas en 32 bytes big-endian.
constexpr std::size_t kG1EncodedBytes = 1 + 2 * 32;
constexpr std::size_t kG2EncodedBytes = 1 + 4 * 32;

std::array<unsigned char, kG1EncodedBytes> encode_point(const bn::Ec1& p);
std::array<unsigned char, kG2EncodedBytes> encode_point(const bn::Ec2& p);

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    Sha256& bytes(const void* data, std::size_t size);
    Sha256& u8(std::uint8_t v);
    Sha256& u64(std::uint64_t v);   // 8 bytes big-endian
    Sha256& i64(std::int64_t v);    // 8 bytes big-endian (complemento a dos)
    Sha256& digest(const Digest& d);
    Sha256& point(const bn::Ec1& p);
    Sha256& point(const bn::Ec2& p);
    Sha256& length_prefixed(const std::vector<unsigned char>& data);  // u64(longitud) + bytes

    Digest finish();

private:
    EVP_MD_CTX* ctx_;
    bool finished_ = false;
};

std::string to_hex(const Digest& d);

}  // namespace tt::crypto

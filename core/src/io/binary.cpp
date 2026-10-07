// binary.cpp

#include "tt/io/binary.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace tt::io {

// ---------------------------------------------------------------------------
// Escritura
// ---------------------------------------------------------------------------

/*
 * Implementacion: agrega el byte al final del buffer.
 */
BinaryWriter& BinaryWriter::u8(std::uint8_t v) {
    buffer_.push_back(v);
    return *this;
}

/*
 * Implementacion: del byte mas significativo al menos significativo.
 */
BinaryWriter& BinaryWriter::u32(std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) buffer_.push_back(static_cast<unsigned char>(v >> shift));
    return *this;
}

/*
 * Implementacion: del byte mas significativo al menos significativo.
 */
BinaryWriter& BinaryWriter::u64(std::uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) buffer_.push_back(static_cast<unsigned char>(v >> shift));
    return *this;
}

/*
 * Implementacion: el complemento a dos es la representacion sin signo.
 */
BinaryWriter& BinaryWriter::i32(std::int32_t v) { return u32(static_cast<std::uint32_t>(v)); }

/*
 * Implementacion: el complemento a dos es la representacion sin signo.
 */
BinaryWriter& BinaryWriter::i64(std::int64_t v) { return u64(static_cast<std::uint64_t>(v)); }

/*
 * Implementacion: copia los bytes al final del buffer.
 */
BinaryWriter& BinaryWriter::raw(const void* data, std::size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    buffer_.insert(buffer_.end(), p, p + size);
    return *this;
}

/*
 * Implementacion: longitud como u32 y luego los bytes.
 */
BinaryWriter& BinaryWriter::bytes(const std::vector<unsigned char>& data) {
    if (data.size() > UINT32_MAX) throw std::length_error("BinaryWriter: cadena demasiado larga");
    u32(static_cast<std::uint32_t>(data.size()));
    return raw(data.data(), data.size());
}

/*
 * Implementacion: longitud como u32 y luego los caracteres.
 */
BinaryWriter& BinaryWriter::text(const std::string& s) {
    if (s.size() > UINT32_MAX) throw std::length_error("BinaryWriter: texto demasiado largo");
    u32(static_cast<std::uint32_t>(s.size()));
    return raw(s.data(), s.size());
}

/*
 * Implementacion: los 32 bytes del digest.
 */
BinaryWriter& BinaryWriter::digest(const crypto::Digest& d) { return raw(d.data(), d.size()); }

/*
 * Implementacion: codificacion canonica de crypto::encode_point.
 */
BinaryWriter& BinaryWriter::point(const bn::Ec1& p) {
    const auto enc = crypto::encode_point(p);
    return raw(enc.data(), enc.size());
}

/*
 * Implementacion: codificacion canonica de crypto::encode_point.
 */
BinaryWriter& BinaryWriter::point(const bn::Ec2& p) {
    const auto enc = crypto::encode_point(p);
    return raw(enc.data(), enc.size());
}

// ---------------------------------------------------------------------------
// Lectura
// ---------------------------------------------------------------------------

/*
 * Implementacion: comprueba que quepa el campo y avanza la posicion.
 */
const unsigned char* BinaryReader::raw(std::size_t size) {
    if (size > remaining()) {
        throw FormatError("datos incompletos: se necesitan " + std::to_string(size) + " bytes en la posicion " +
                          std::to_string(pos_) + " y quedan " + std::to_string(remaining()));
    }
    const unsigned char* p = data_ + pos_;
    pos_ += size;
    return p;
}

/*
 * Implementacion: un byte con raw().
 */
std::uint8_t BinaryReader::u8() { return *raw(1); }

/*
 * Implementacion: 4 bytes big-endian.
 */
std::uint32_t BinaryReader::u32() {
    const unsigned char* p = raw(4);
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | p[i];
    return v;
}

/*
 * Implementacion: 8 bytes big-endian.
 */
std::uint64_t BinaryReader::u64() {
    const unsigned char* p = raw(8);
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

/*
 * Implementacion: lee sin signo y reinterpreta en complemento a dos.
 */
std::int32_t BinaryReader::i32() { return static_cast<std::int32_t>(u32()); }

/*
 * Implementacion: lee sin signo y reinterpreta en complemento a dos.
 */
std::int64_t BinaryReader::i64() { return static_cast<std::int64_t>(u64()); }

/*
 * Implementacion: lee la longitud, la compara con el maximo y copia.
 */
std::vector<unsigned char> BinaryReader::bytes(std::size_t max_size) {
    const std::uint32_t size = u32();
    if (size > max_size) throw FormatError("cadena de " + std::to_string(size) + " bytes; el maximo es " +
                                           std::to_string(max_size));
    const unsigned char* p = raw(size);
    return std::vector<unsigned char>(p, p + size);
}

/*
 * Implementacion: igual que bytes(), devolviendo un std::string.
 */
std::string BinaryReader::text(std::size_t max_size) {
    const std::uint32_t size = u32();
    if (size > max_size) throw FormatError("texto de " + std::to_string(size) + " bytes; el maximo es " +
                                           std::to_string(max_size));
    const unsigned char* p = raw(size);
    return std::string(reinterpret_cast<const char*>(p), size);
}

/*
 * Implementacion: copia 32 bytes.
 */
crypto::Digest BinaryReader::digest() {
    crypto::Digest d{};
    const unsigned char* p = raw(d.size());
    std::copy(p, p + d.size(), d.begin());
    return d;
}

/*
 * Implementacion: decodifica con crypto::decode_g1 y convierte su error en
 * FormatError con la posicion.
 */
bn::Ec1 BinaryReader::g1() {
    const std::size_t at = pos_;
    const unsigned char* p = raw(crypto::kG1EncodedBytes);
    try {
        return crypto::decode_g1(p);
    } catch (const std::invalid_argument& e) {
        throw FormatError(std::string(e.what()) + " (posicion " + std::to_string(at) + ")");
    }
}

/*
 * Implementacion: decodifica con crypto::decode_g2 y convierte su error en
 * FormatError con la posicion.
 */
bn::Ec2 BinaryReader::g2() {
    const std::size_t at = pos_;
    const unsigned char* p = raw(crypto::kG2EncodedBytes);
    try {
        return crypto::decode_g2(p);
    } catch (const std::invalid_argument& e) {
        throw FormatError(std::string(e.what()) + " (posicion " + std::to_string(at) + ")");
    }
}

/*
 * Implementacion: compara con lo que queda, cuidando que la multiplicacion
 * no se desborde.
 */
void BinaryReader::require_items(std::uint64_t count, std::size_t item_size) const {
    if (item_size != 0 && count > remaining() / item_size) {
        throw FormatError("el archivo anuncia " + std::to_string(count) + " elementos pero no le alcanzan los datos");
    }
}

/*
 * Implementacion: compara la posicion con el tamano.
 */
void BinaryReader::expect_end() const {
    if (remaining() != 0) throw FormatError("sobran " + std::to_string(remaining()) + " bytes al final");
}

}  // namespace tt::io

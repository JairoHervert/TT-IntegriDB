// hash.cpp

#include "tt/crypto/hash.hpp"

#include <openssl/evp.h>

#include <stdexcept>
#include <string>

#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/pairing_group.hpp"

namespace tt::crypto {

namespace {

/*
 * Convierte un caracter hexadecimal ('0'-'9', 'a'-'f', 'A'-'F') en su
 * valor de 0 a 15.
 *
 * Errores:
 *   Lanza std::logic_error si el caracter no es hexadecimal (no deberia
 *   pasar: solo recibe texto generado por ate-pairing).
 */
int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::logic_error("hash: digito hexadecimal inesperado");
}

/*
 * Escribe un elemento de F_p (una coordenada de un punto) como 32 bytes
 * big-endian en `out`.
 *
 * Pasa por la representacion hexadecimal que ofrece ate-pairing ("0x...")
 * para no depender de como guarda los numeros internamente: quita el
 * prefijo, rellena con ceros a la izquierda hasta 64 digitos y convierte
 * cada par de digitos en un byte.
 *
 * Parametros:
 *   x    coordenada
 *   out  destino con espacio para 32 bytes
 *
 * Errores:
 *   Lanza std::logic_error si la coordenada tuviera mas de 256 bits.
 */
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

/*
 * Lee 32 bytes big-endian como un elemento de F_p. Exige que el numero sea
 * menor que p, para que cada coordenada tenga una sola codificacion.
 *
 * Errores:
 *   Lanza std::invalid_argument si el numero es mayor o igual que p.
 */
bn::Fp read_coordinate(const unsigned char* in) {
    static const char* const kDigits = "0123456789abcdef";
    std::string hex = "0x";
    hex.reserve(66);
    for (std::size_t i = 0; i < 32; ++i) {
        hex.push_back(kDigits[in[i] >> 4]);
        hex.push_back(kDigits[in[i] & 0x0f]);
    }
    if (!(mie::Vuint(hex) < bn::Param::p)) {
        throw std::invalid_argument("punto: coordenada mayor o igual que p");
    }
    bn::Fp x;
    x.set(hex);
    return x;
}

/*
 * Revisa la marca de un punto codificado. Devuelve true si es un punto
 * afin (marca 1) y false si es el punto al infinito (marca 0 con todas las
 * coordenadas en cero).
 *
 * Errores:
 *   Lanza std::invalid_argument si la marca no es 0 ni 1, o si el punto al
 *   infinito trae coordenadas distintas de cero.
 */
bool read_marker(const unsigned char* data, std::size_t size) {
    if (data[0] == 1) return true;
    if (data[0] != 0) throw std::invalid_argument("punto: marca invalida");
    for (std::size_t i = 1; i < size; ++i) {
        if (data[i] != 0) throw std::invalid_argument("punto: el punto al infinito debe ir en ceros");
    }
    return false;
}

}  // namespace

/*
 * Implementacion: normaliza el punto a coordenadas afines (z = 1) y escribe
 * x e y. El punto al infinito queda todo en ceros.
 */
std::array<unsigned char, kG1EncodedBytes> encode_point(const bn::Ec1& p) {
    std::array<unsigned char, kG1EncodedBytes> out{};
    if (p.isZero()) return out;  // marca 0 y coordenadas en cero
    p.normalize();
    out[0] = 1;
    write_coordinate(p.p[0], out.data() + 1);
    write_coordinate(p.p[1], out.data() + 33);
    return out;
}

/*
 * Implementacion: igual que en G1, escribiendo las dos componentes (a, b) de
 * cada coordenada.
 */
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

/*
 * Implementacion: asegura que el grupo este inicializado (para conocer p),
 * revisa la marca, lee x e y con read_coordinate y comprueba que el punto
 * este en la curva.
 */
bn::Ec1 decode_g1(const unsigned char* data) {
    init_pairing_group();  // fija p y los parametros de la curva en este hilo
    if (!read_marker(data, kG1EncodedBytes)) return zero_point<bn::Ec1>();
    bn::Ec1 p;
    p.set(read_coordinate(data + 1), read_coordinate(data + 33), false);
    if (!p.isValid()) throw std::invalid_argument("punto: no esta en la curva de G1");
    return p;
}

/*
 * Implementacion: igual que en G1, leyendo las dos componentes (a, b) de
 * cada coordenada.
 */
bn::Ec2 decode_g2(const unsigned char* data) {
    init_pairing_group();
    if (!read_marker(data, kG2EncodedBytes)) return zero_point<bn::Ec2>();
    bn::Ec2 p;
    p.set(bn::Fp2(read_coordinate(data + 1), read_coordinate(data + 33)),
          bn::Fp2(read_coordinate(data + 65), read_coordinate(data + 97)), false);
    if (!p.isValid()) throw std::invalid_argument("punto: no esta en la curva torcida de G2");
    return p;
}

/*
 * Implementacion: crea un contexto EVP de OpenSSL configurado para SHA-256.
 */
Sha256::Sha256() : ctx_(EVP_MD_CTX_new()) {
    if (ctx_ == nullptr || EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx_);
        throw std::runtime_error("SHA-256: no se pudo inicializar");
    }
}

/*
 * Implementacion: EVP_MD_CTX_free acepta nullptr, asi que es seguro siempre.
 */
Sha256::~Sha256() { EVP_MD_CTX_free(ctx_); }

/*
 * Implementacion: pasa los bytes a EVP_DigestUpdate.
 */
Sha256& Sha256::bytes(const void* data, std::size_t size) {
    if (finished_) throw std::logic_error("SHA-256: se agregaron datos despues de finish()");
    if (size > 0 && EVP_DigestUpdate(ctx_, data, size) != 1) {
        throw std::runtime_error("SHA-256: fallo EVP_DigestUpdate");
    }
    return *this;
}

/*
 * Implementacion: agrega el byte directamente.
 */
Sha256& Sha256::u8(std::uint8_t v) { return bytes(&v, 1); }

/*
 * Implementacion: separa el entero en 8 bytes, del mas significativo al
 * menos significativo.
 */
Sha256& Sha256::u64(std::uint64_t v) {
    unsigned char buf[8];
    for (int i = 7; i >= 0; --i) {
        buf[i] = static_cast<unsigned char>(v & 0xff);
        v >>= 8;
    }
    return bytes(buf, sizeof buf);
}

/*
 * Implementacion: reinterpreta el entero con signo como sin signo, que es
 * exactamente su representacion en complemento a dos.
 */
Sha256& Sha256::i64(std::int64_t v) { return u64(static_cast<std::uint64_t>(v)); }

/*
 * Implementacion: agrega los 32 bytes del digest.
 */
Sha256& Sha256::digest(const Digest& d) { return bytes(d.data(), d.size()); }

/*
 * Implementacion: codifica el punto con encode_point y agrega los bytes.
 */
Sha256& Sha256::point(const bn::Ec1& p) {
    const auto enc = encode_point(p);
    return bytes(enc.data(), enc.size());
}

/*
 * Implementacion: codifica el punto con encode_point y agrega los bytes.
 */
Sha256& Sha256::point(const bn::Ec2& p) {
    const auto enc = encode_point(p);
    return bytes(enc.data(), enc.size());
}

/*
 * Implementacion: agrega la longitud como u64 y despues los bytes.
 */
Sha256& Sha256::length_prefixed(const std::vector<unsigned char>& data) {
    u64(data.size());
    return bytes(data.data(), data.size());
}

/*
 * Implementacion: EVP_DigestFinal_ex escribe los 32 bytes del resultado.
 */
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

/*
 * Implementacion: cada byte se convierte en dos digitos hexadecimales.
 */
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

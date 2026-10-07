// symmetric.cpp

#include "tt/crypto/symmetric.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>

namespace tt::crypto {

namespace {

using Mac = std::array<unsigned char, 32>;

/*
 * Calcula HMAC-SHA256 de `data` con la clave `key` usando OpenSSL.
 *
 * Parametros:
 *   key, key_size    clave del HMAC
 *   data, data_size  mensaje
 *
 * Devuelve:
 *   Los 32 bytes del HMAC.
 *
 * Errores:
 *   Lanza std::runtime_error si OpenSSL falla.
 */
Mac hmac_sha256(const unsigned char* key, std::size_t key_size, const unsigned char* data,
                std::size_t data_size) {
    Mac out{};
    std::size_t out_size = 0;
    if (EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr, key, key_size, data, data_size,
                  out.data(), out.size(), &out_size) == nullptr ||
        out_size != out.size()) {
        throw std::runtime_error("HMAC-SHA256: fallo EVP_Q_mac");
    }
    return out;
}

/*
 * Deriva una subclave a partir de la clave del propietario y una etiqueta:
 * HMAC(clave, etiqueta). Etiquetas distintas dan subclaves independientes,
 * asi la clave original nunca se usa directamente en dos primitivas.
 */
Mac derive(const AesKey& key, const char* label) {
    return hmac_sha256(key.data(), key.size(), reinterpret_cast<const unsigned char*>(label),
                       std::strlen(label));
}

// Subclaves derivadas de la clave del propietario. Se borran de memoria al
// destruirse.
struct DerivedKeys {
    Mac aes;  // se usan los primeros 16 bytes
    Mac iv;

    /*
     * Borra las subclaves de memoria para que no queden en el proceso.
     */
    ~DerivedKeys() {
        OPENSSL_cleanse(aes.data(), aes.size());
        OPENSSL_cleanse(iv.data(), iv.size());
    }
};

/*
 * Obtiene las dos subclaves que usa el cifrado: una para AES y otra para
 * calcular el IV sintetico.
 */
DerivedKeys derive_keys(const AesKey& key) {
    return DerivedKeys{derive(key, "tt-integridb/enc/aes-128-cbc"),
                       derive(key, "tt-integridb/enc/synthetic-iv")};
}

/*
 * Calcula el IV sintetico de un mensaje: los primeros 16 bytes de
 * HMAC(k_iv, mensaje). Lo usan tanto el cifrado (para elegir el IV) como el
 * descifrado (para comprobar que el mensaje no cambio).
 */
std::array<unsigned char, kAesBlockBytes> synthetic_iv(const DerivedKeys& keys,
                                                       const std::vector<unsigned char>& message) {
    const Mac mac = hmac_sha256(keys.iv.data(), keys.iv.size(), message.data(), message.size());
    std::array<unsigned char, kAesBlockBytes> iv{};
    std::memcpy(iv.data(), mac.data(), iv.size());
    return iv;
}

// Libera un contexto de cifrado de OpenSSL; permite usarlo con unique_ptr
// para que se libere aunque haya una excepcion.
struct CipherCtxDeleter {
    /*
     * Libera el contexto recibido.
     */
    void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter>;

}  // namespace

/*
 * Implementacion: deriva las subclaves, calcula el IV a partir del mensaje,
 * escribe el IV al inicio de la salida y a continuacion el cifrado AES-CBC
 * con relleno PKCS#7.
 */
std::vector<unsigned char> encrypt_deterministic(const AesKey& key,
                                                 const std::vector<unsigned char>& plaintext) {
    const DerivedKeys keys = derive_keys(key);
    const auto iv = synthetic_iv(keys, plaintext);

    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr, keys.aes.data(), iv.data()) != 1) {
        throw std::runtime_error("AES: no se pudo inicializar el cifrado");
    }
    std::vector<unsigned char> out(iv.size() + plaintext.size() + kAesBlockBytes);
    std::memcpy(out.data(), iv.data(), iv.size());
    int len1 = 0;
    int len2 = 0;
    if (EVP_EncryptUpdate(ctx.get(), out.data() + iv.size(), &len1, plaintext.data(),
                          static_cast<int>(plaintext.size())) != 1 ||
        EVP_EncryptFinal_ex(ctx.get(), out.data() + iv.size() + len1, &len2) != 1) {
        throw std::runtime_error("AES: fallo al cifrar");
    }
    out.resize(iv.size() + static_cast<std::size_t>(len1 + len2));
    return out;
}

/*
 * Implementacion: valida la longitud, descifra con el IV recibido, quita el
 * relleno y al final recalcula el IV sintetico del mensaje obtenido. La
 * comparacion se hace en tiempo constante (CRYPTO_memcmp) y, si falla, el
 * mensaje se borra de memoria antes de lanzar el error.
 */
std::vector<unsigned char> decrypt_deterministic(const AesKey& key,
                                                 const std::vector<unsigned char>& sealed) {
    if (sealed.size() < 2 * kAesBlockBytes || sealed.size() % kAesBlockBytes != 0) {
        throw DecryptionError("cifrado con longitud invalida");
    }
    const DerivedKeys keys = derive_keys(key);

    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr, keys.aes.data(), sealed.data()) != 1) {
        throw std::runtime_error("AES: no se pudo inicializar el descifrado");
    }
    const std::size_t body = sealed.size() - kAesBlockBytes;
    std::vector<unsigned char> plain(body + kAesBlockBytes);
    int len1 = 0;
    int len2 = 0;
    if (EVP_DecryptUpdate(ctx.get(), plain.data(), &len1, sealed.data() + kAesBlockBytes,
                          static_cast<int>(body)) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), plain.data() + len1, &len2) != 1) {
        throw DecryptionError("el cifrado fue alterado (relleno invalido)");
    }
    plain.resize(static_cast<std::size_t>(len1 + len2));

    const auto expected_iv = synthetic_iv(keys, plain);
    if (CRYPTO_memcmp(expected_iv.data(), sealed.data(), kAesBlockBytes) != 0) {
        OPENSSL_cleanse(plain.data(), plain.size());
        throw DecryptionError("el cifrado fue alterado (el IV no corresponde al mensaje)");
    }
    return plain;
}

}  // namespace tt::crypto

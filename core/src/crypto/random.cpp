// random.cpp

#include "tt/crypto/random.hpp"

#include <NTL/ZZ.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <stdexcept>

#include "tt/crypto/pairing_group.hpp"

namespace tt::crypto {

/*
 * Implementacion: reserva el vector y lo llena con RAND_bytes, que devuelve
 * 1 solo si pudo generar todos los bytes con entropia suficiente.
 */
std::vector<unsigned char> random_bytes(std::size_t count) {
    std::vector<unsigned char> out(count);
    if (count > 0 && RAND_bytes(out.data(), static_cast<int>(count)) != 1) {
        throw std::runtime_error("RAND_bytes fallo: no hay aleatoriedad segura disponible");
    }
    return out;
}

/*
 * Implementacion: toma 64 bytes aleatorios (512 bits) y los reduce modulo r.
 * Como 512 bits es mucho mas que los 254 bits de r, el resultado es
 * practicamente uniforme. Si por azar sale cero, se repite. El buffer se
 * borra de memoria antes de salir.
 */
NTL::ZZ_p random_nonzero_scalar() {
    init_pairing_group();  // fija el modulo r en este hilo

    // 64 bytes = 512 bits reducidos modulo r (254 bits): el sesgo respecto a
    // la distribucion uniforme es menor que 2^-250, despreciable.
    constexpr std::size_t kBytes = 64;
    while (true) {
        std::vector<unsigned char> buf = random_bytes(kBytes);
        NTL::ZZ value;
        NTL::ZZFromBytes(value, buf.data(), static_cast<long>(buf.size()));
        OPENSSL_cleanse(buf.data(), buf.size());

        NTL::ZZ_p scalar = NTL::conv<NTL::ZZ_p>(value);
        if (!NTL::IsZero(scalar)) return scalar;
    }
}

}  // namespace tt::crypto

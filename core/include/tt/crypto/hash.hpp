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

/*
 * Codifica un punto de G1 en bytes de forma canonica: el mismo punto da
 * siempre los mismos 65 bytes, sin importar como lo tenga guardado
 * internamente ate-pairing (coordenadas proyectivas distintas).
 *
 * Formato: 1 byte de marca (0 = punto al infinito, 1 = punto afin) seguido
 * de las coordenadas x, y en 32 bytes big-endian cada una (en cero si es el
 * punto al infinito).
 *
 * Parametros:
 *   p  punto de G1
 *
 * Devuelve:
 *   Arreglo de 65 bytes.
 */
std::array<unsigned char, kG1EncodedBytes> encode_point(const bn::Ec1& p);

/*
 * Igual que la version de G1, para un punto de G2. Cada coordenada de G2
 * tiene dos componentes (a, b), asi que son 1 + 4 x 32 = 129 bytes.
 *
 * Parametros:
 *   p  punto de G2
 *
 * Devuelve:
 *   Arreglo de 129 bytes.
 */
std::array<unsigned char, kG2EncodedBytes> encode_point(const bn::Ec2& p);

/*
 * Operacion inversa de encode_point para G1: lee 65 bytes y reconstruye el
 * punto. Solo acepta la codificacion canonica: marca 0 con coordenadas en
 * cero (punto al infinito) o marca 1 con coordenadas menores que p que
 * esten en la curva.
 *
 * Parametros:
 *   data  65 bytes (kG1EncodedBytes)
 *
 * Devuelve:
 *   El punto.
 *
 * Errores:
 *   Lanza std::invalid_argument si los bytes no son una codificacion valida.
 */
bn::Ec1 decode_g1(const unsigned char* data);

/*
 * Operacion inversa de encode_point para G2: lee 129 bytes con las mismas
 * reglas que decode_g1 y revisa que el punto este en la curva torcida.
 *
 * No revisa que el punto este en el subgrupo de orden r (cuesta una
 * multiplicacion escalar completa). Quien lo use debe comprobarlo de otra
 * forma; por ejemplo, la base autenticada compara el hash de cada nodo con
 * el digest del propietario, y ese hash cubre estos bytes.
 *
 * Parametros:
 *   data  129 bytes (kG2EncodedBytes)
 *
 * Devuelve:
 *   El punto.
 *
 * Errores:
 *   Lanza std::invalid_argument si los bytes no son una codificacion valida.
 */
bn::Ec2 decode_g2(const unsigned char* data);

/*
 * Calcula un SHA-256 agregando los datos por partes, cada una con su
 * codificacion canonica. Los metodos devuelven el mismo objeto para poder
 * encadenarlos:
 *
 *     crypto::Sha256 h;
 *     h.u8(etiqueta).i64(clave).u64(contador).point(acumulador);
 *     Digest d = h.finish();
 *
 * No se puede copiar (envuelve un contexto de OpenSSL).
 */
class Sha256 {
public:
    /*
     * Crea un calculo de SHA-256 vacio.
     *
     * Errores:
     *   Lanza std::runtime_error si OpenSSL no puede inicializarlo.
     */
    Sha256();

    /*
     * Libera el contexto de OpenSSL.
     */
    ~Sha256();

    /*
     * No se permite copiar un Sha256: la copia compartiria el mismo contexto de
     * OpenSSL y ambos objetos lo liberarian al destruirse.
     */
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    /*
     * Agrega `size` bytes tal cual, sin codificacion adicional. Las demas
     * funciones de agregado se apoyan en esta.
     *
     * Errores:
     *   Lanza std::logic_error si ya se llamo a finish().
     */
    Sha256& bytes(const void* data, std::size_t size);

    /*
     * Agrega un byte. Se usa para las etiquetas de dominio, que distinguen
     * un tipo de hash de otro (hoja, nodo interno, etc.).
     */
    Sha256& u8(std::uint8_t v);

    /*
     * Agrega un entero sin signo de 64 bits como 8 bytes big-endian.
     */
    Sha256& u64(std::uint64_t v);

    /*
     * Agrega un entero con signo de 64 bits como 8 bytes big-endian en
     * complemento a dos (por ejemplo, -1 son ocho bytes 0xff).
     */
    Sha256& i64(std::int64_t v);

    /*
     * Agrega un digest de 32 bytes (por ejemplo, el hash de un nodo hijo).
     */
    Sha256& digest(const Digest& d);

    /*
     * Agrega un punto de G1 con su codificacion canonica (encode_point).
     */
    Sha256& point(const bn::Ec1& p);

    /*
     * Agrega un punto de G2 con su codificacion canonica (encode_point).
     */
    Sha256& point(const bn::Ec2& p);

    /*
     * Agrega una cadena de bytes de longitud variable precedida por su
     * longitud (u64). Sin el prefijo, ("ab", "c") y ("a", "bc") darian el
     * mismo hash.
     */
    Sha256& length_prefixed(const std::vector<unsigned char>& data);

    /*
     * Termina el calculo y devuelve el hash. Despues de esto el objeto ya no
     * acepta mas datos.
     *
     * Devuelve:
     *   Los 32 bytes del SHA-256.
     *
     * Errores:
     *   Lanza std::logic_error si se llama dos veces.
     */
    Digest finish();

private:
    EVP_MD_CTX* ctx_;
    bool finished_ = false;
};

/*
 * Escribe un digest como 64 digitos hexadecimales en minusculas. Util para
 * mostrar hashes en pantalla, en bitacoras y en las pruebas.
 */
std::string to_hex(const Digest& d);

}  // namespace tt::crypto

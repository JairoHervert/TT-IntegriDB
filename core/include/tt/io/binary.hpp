// binary.hpp
//
// Lectura y escritura de datos binarios con formato fijo: enteros en
// big-endian, cadenas con su longitud por delante y puntos de G1/G2 con la
// codificacion canonica de tt/crypto/hash.hpp. La usan los archivos del
// esquema (la base autenticada D~) y la usaran los mensajes entre el
// probador y el verificador.
//
// El lector nunca confia en el archivo: revisa que cada campo quepa en lo
// que queda de datos antes de leerlo o de reservar memoria, asi un archivo
// truncado o alterado produce un FormatError claro y no un fallo de memoria.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "bn.h"
#include "tt/crypto/hash.hpp"

namespace tt::io {

// Error de formato al leer datos binarios.
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Escribe campos uno tras otro en un buffer en memoria.
class BinaryWriter {
public:
    /*
     * Agrega un byte.
     */
    BinaryWriter& u8(std::uint8_t v);

    /*
     * Agrega un entero sin signo de 32 bits (4 bytes big-endian).
     */
    BinaryWriter& u32(std::uint32_t v);

    /*
     * Agrega un entero sin signo de 64 bits (8 bytes big-endian).
     */
    BinaryWriter& u64(std::uint64_t v);

    /*
     * Agrega un entero con signo de 32 bits en complemento a dos.
     */
    BinaryWriter& i32(std::int32_t v);

    /*
     * Agrega un entero con signo de 64 bits en complemento a dos.
     */
    BinaryWriter& i64(std::int64_t v);

    /*
     * Agrega bytes tal cual, sin longitud.
     */
    BinaryWriter& raw(const void* data, std::size_t size);

    /*
     * Agrega una cadena de bytes precedida por su longitud (u32).
     */
    BinaryWriter& bytes(const std::vector<unsigned char>& data);

    /*
     * Agrega un texto precedido por su longitud (u32).
     */
    BinaryWriter& text(const std::string& s);

    /*
     * Agrega un digest de 32 bytes.
     */
    BinaryWriter& digest(const crypto::Digest& d);

    /*
     * Agrega un punto de G1 (65 bytes, codificacion canonica).
     */
    BinaryWriter& point(const bn::Ec1& p);

    /*
     * Agrega un punto de G2 (129 bytes, codificacion canonica).
     */
    BinaryWriter& point(const bn::Ec2& p);

    /*
     * Devuelve todo lo escrito hasta ahora.
     */
    const std::vector<unsigned char>& data() const { return buffer_; }

private:
    std::vector<unsigned char> buffer_;
};

// Lee campos en el mismo orden en que se escribieron.
class BinaryReader {
public:
    /*
     * Prepara la lectura de `size` bytes a partir de `data`. Los bytes deben
     * seguir existiendo mientras se use el lector.
     */
    BinaryReader(const unsigned char* data, std::size_t size) : data_(data), size_(size) {}

    /*
     * Lee un byte.
     */
    std::uint8_t u8();

    /*
     * Lee un entero sin signo de 32 bits.
     */
    std::uint32_t u32();

    /*
     * Lee un entero sin signo de 64 bits.
     */
    std::uint64_t u64();

    /*
     * Lee un entero con signo de 32 bits.
     */
    std::int32_t i32();

    /*
     * Lee un entero con signo de 64 bits.
     */
    std::int64_t i64();

    /*
     * Devuelve un puntero a los siguientes `size` bytes y avanza.
     */
    const unsigned char* raw(std::size_t size);

    /*
     * Lee una cadena de bytes con longitud; rechaza longitudes mayores que
     * `max_size`.
     */
    std::vector<unsigned char> bytes(std::size_t max_size);

    /*
     * Lee un texto con longitud; rechaza longitudes mayores que `max_size`.
     */
    std::string text(std::size_t max_size);

    /*
     * Lee un digest de 32 bytes.
     */
    crypto::Digest digest();

    /*
     * Lee un punto de G1 (ver crypto::decode_g1).
     */
    bn::Ec1 g1();

    /*
     * Lee un punto de G2 (ver crypto::decode_g2). Revisa que este en la
     * curva, pero no que este en el subgrupo de orden r. Para D~ basta,
     * porque cada punto queda cubierto por un hash que se compara con
     * delta; cuando se lean puntos que el verificador recibe del probador
     * sin ese respaldo, hay que comprobar ademas
     * crypto::in_prime_order_subgroup.
     */
    bn::Ec2 g2();

    /*
     * Revisa que queden al menos count * item_size bytes. Se llama antes de
     * reservar memoria para `count` elementos que el archivo dice tener.
     *
     * Errores:
     *   Lanza FormatError si no alcanzan los datos (archivo truncado o un
     *   contador alterado).
     */
    void require_items(std::uint64_t count, std::size_t item_size) const;

    /*
     * Bytes que faltan por leer.
     */
    std::size_t remaining() const { return size_ - pos_; }

    /*
     * Posicion actual (bytes ya leidos).
     */
    std::size_t position() const { return pos_; }

    /*
     * Exige que ya no queden datos (para rechazar archivos con basura al
     * final).
     *
     * Errores:
     *   Lanza FormatError si sobran bytes.
     */
    void expect_end() const;

private:
    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

}  // namespace tt::io

// digest.hpp
//
// Digest delta: el resumen publico del estado autenticado de la base
// (algoritmo Setup del reporte). El propietario lo publica y el verificador
// revisa cada respuesta contra el. Contiene:
//
//   - la version del estado (1 despues de Setup; cada actualizacion la
//     incrementa), para que el cliente sepa si tiene el digest vigente
//     (RNF08);
//   - la huella de la clave publica con la que se construyo;
//   - por cada tabla: su estructura, su numero de filas y la raiz del arbol
//     de Merkle de sus filas (en lugar de los n hashes h_r del algoritmo;
//     ver tt/merkle/tree.hpp);
//   - por cada AIT construido: su tabla, columna clave, columna valor y la
//     raiz delta^T_{i x j}.
//
// Todo eso se resume en un identificador id() de 32 bytes (SHA-256 con la
// etiqueta 0x20 sobre una codificacion canonica). Dos digests con el mismo
// id describen exactamente el mismo estado.
//
// En disco, delta es un archivo de texto, pequeno y legible, para que se
// pueda revisar a simple vista:
//
//   tt-integridb digest v1
//   state_version 1
//   key_fingerprint <64 hex>
//   plan queries
//   table academic_record reference record_id rows 15171 root <64 hex>
//   column academic_record record_id integer
//   ...
//   tree academic_record grade record_id leaves 6 values 15171 root <64 hex>
//   ...
//   id <64 hex>
//   end
//
// Al leerlo se recalcula el id y se compara con la linea "id": si alguien
// edita el archivo (o se dana), la lectura falla.

#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt/crypto/hash.hpp"
#include "tt/db/table.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace tt::scheme {

// Lo que delta dice de una tabla.
struct TableDigest {
    db::TableSchema schema;       // nombre, columnas y columna de referencia
    std::uint64_t row_count = 0;  // numero de filas (hojas del arbol de Merkle)
    crypto::Digest rows_root{};   // raiz del arbol de Merkle de sus filas

    /*
     * Compara todos los campos.
     */
    bool operator==(const TableDigest& o) const {
        return schema == o.schema && row_count == o.row_count && rows_root == o.rows_root;
    }
};

// Lo que delta dice de un AIT.
struct TreeDigest {
    TreeSpec spec;
    std::uint64_t leaf_count = 0;   // claves distintas
    std::uint64_t value_count = 0;  // filas (siempre igual al numero de filas de la tabla)
    crypto::Digest root{};          // delta^T_{i x j}: hash de la raiz del AIT

    /*
     * Compara todos los campos.
     */
    bool operator==(const TreeDigest& o) const {
        return spec == o.spec && leaf_count == o.leaf_count && value_count == o.value_count && root == o.root;
    }
};

// El digest delta completo.
struct DatabaseDigest {
    std::uint64_t state_version = 1;
    crypto::Digest key_fingerprint{};
    std::string plan;                 // plan de arboles con que se construyo (informativo)
    std::vector<TableDigest> tables;  // ordenadas por nombre
    std::vector<TreeDigest> trees;    // en orden canonico (TreeSpec::operator<)

    /*
     * Identificador del estado: SHA-256 con la etiqueta 0x20 sobre la
     * codificacion canonica de todos los campos. Es el valor que el
     * verificador compara para saber si el probador respondio sobre el
     * mismo estado que el tiene.
     */
    crypto::Digest id() const;

    /*
     * Busca una tabla por nombre.
     *
     * Devuelve:
     *   Puntero a su entrada, o nullptr si no esta.
     */
    const TableDigest* find_table(const std::string& table) const;

    /*
     * Busca un AIT.
     *
     * Devuelve:
     *   Puntero a su entrada, o nullptr si ese arbol no se construyo (la
     *   consulta que lo necesite no se puede verificar).
     */
    const TreeDigest* find_tree(const TreeSpec& spec) const;

    /*
     * Compara todos los campos.
     */
    bool operator==(const DatabaseDigest& o) const {
        return state_version == o.state_version && key_fingerprint == o.key_fingerprint && plan == o.plan &&
               tables == o.tables && trees == o.trees;
    }
};

// Error de formato o de consistencia en un digest.
class DigestFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Revisa que un digest sea coherente: version mayor que cero, plan con
 * nombre valido, tablas validas en orden y sin repetir, y cada AIT sobre
 * una tabla y columnas enteras existentes, en orden canonico, sin repetir
 * y con tantos valores como filas tiene su tabla.
 *
 * Errores:
 *   Lanza DigestFormatError con el problema.
 */
void validate_digest(const DatabaseDigest& digest);

/*
 * Escribe el digest en el formato de texto descrito arriba, terminando con
 * su id y "end".
 *
 * Errores:
 *   Lanza DigestFormatError si el digest no es coherente (validate_digest).
 */
std::string digest_to_text(const DatabaseDigest& digest);

/*
 * Lee un digest del formato de texto. Es estricto: exige cada linea en su
 * lugar, numeros y hexadecimales en forma canonica, y que el id escrito
 * coincida con el recalculado.
 *
 * Errores:
 *   Lanza DigestFormatError con el numero de linea y el problema.
 */
DatabaseDigest digest_from_text(const std::string& text);

/*
 * Guarda el digest en un archivo (escritura atomica).
 */
void save_digest(const std::filesystem::path& path, const DatabaseDigest& digest);

/*
 * Carga un digest de un archivo con digest_from_text.
 *
 * Errores:
 *   Lanza std::runtime_error si no se puede abrir y DigestFormatError (con
 *   la ruta) si el contenido no es valido.
 */
DatabaseDigest load_digest(const std::filesystem::path& path);

/*
 * Convierte 64 digitos hexadecimales en minusculas en un digest.
 *
 * Devuelve:
 *   true y el digest en `out` si el texto es valido; false si no.
 */
bool parse_hex_digest(const std::string& hex, crypto::Digest& out);

}  // namespace tt::scheme

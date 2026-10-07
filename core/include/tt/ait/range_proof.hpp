// range_proof.hpp
//
// Prueba de RangeCover y su verificacion contra el digest del AIT.
//
// La prueba es el "arbol podado": la parte del AIT que hace falta para
// recalcular el hash de la raiz. Se recorre en preorden y cada nodo aparece
// de una de tres formas:
//
//   kCover     nodo cuyo subarbol esta completo dentro del rango: forma parte
//              del conjunto de cobertura. Lleva su encabezado, su valor v_u
//              (acumuladores y cifrado) y, si es interno, los hashes de sus
//              hijos. No se desciende mas.
//   kExpanded  nodo que corta el rango, o que esta fuera pero hay que
//              mostrarlo para justificarlo. Lleva su encabezado y H(v_u); si
//              es interno, le siguen sus dos hijos.
//   kPruned    subarbol completo fuera del rango. Solo lleva su hash. Solo se
//              permite donde las reglas de node.hpp lo justifican con los
//              datos del padre.
//
// El verificador recalcula el hash de la raiz y lo compara con el digest
// vigente. Ademas exige la forma canonica (todo nodo completo dentro del
// rango es kCover; toda poda esta justificada). Con eso, cada hoja del arbol
// cae en un nodo de cobertura (dentro del rango) o en una parte podada o
// expandida que demostradamente esta fuera: el servidor no puede omitir
// filas, agregar filas ni cambiar acumuladores sin que el hash de la raiz
// deje de coincidir.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tt/ait/node.hpp"

namespace tt::ait {

// Forma en que aparece un nodo dentro de la prueba (ver arriba).
enum class EntryKind : std::uint8_t { kCover = 0, kExpanded = 1, kPruned = 2 };

// Una entrada de la prueba. Segun `kind`, solo algunos campos tienen datos.
struct ProofEntry {
    EntryKind kind = EntryKind::kPruned;
    NodeHeader header;       // kCover, kExpanded
    NodePayload payload;     // kCover
    Digest payload_digest{}; // kExpanded
    Digest left_hash{};      // kCover interno
    Digest right_hash{};     // kCover interno
    Digest hash{};           // kPruned
};

// Prueba completa de un RangeCover.
struct RangeProof {
    std::vector<ProofEntry> entries;  // preorden; vacia si el arbol no tiene filas
};

// Respuesta del probador a RangeCover(lo, hi).
struct RangeAnswer {
    // Valores (columna j) de todas las hojas con clave en [lo, hi], en orden
    // de clave; dentro de una misma clave, en orden ascendente.
    std::vector<Value> values;
    RangeProof proof;
};

// Nodo del conjunto de cobertura, ya autenticado contra el digest.
struct CoverNode {
    NodeHeader header;
    NodePayload payload;
};

// Resultado de verify_range.
struct RangeVerification {
    bool accepted = false;
    std::string reason;             // por que se rechazo (vacio si se acepto)
    std::vector<CoverNode> cover;   // en orden de clave
    std::uint64_t count = 0;        // filas en el rango (suma de los contadores)
};

/*
 * Verifica la prueba de RangeCover(lo, hi) contra el digest vigente del AIT.
 * Es la parte del procedimiento Verify que corresponde al AIT.
 *
 * Recalcula el hash de la raiz a partir de la prueba y lo compara con
 * root_digest; ademas exige la forma canonica (todo nodo completo dentro
 * del rango va como cobertura y toda poda esta justificada). No usa
 * emparejamientos, solo hashes, por eso es muy rapida.
 *
 * Parametros:
 *   root_digest  digest vigente del AIT (lo publica el propietario en delta)
 *   lo, hi       limites del rango (inclusivos)
 *   proof        prueba enviada por el servidor
 *
 * Devuelve:
 *   accepted = true y los nodos de cobertura autenticados (en orden de
 *   clave) con su total de filas, si la prueba es valida. Si no,
 *   accepted = false, reason explica el motivo y cover queda vacio: nada
 *   de lo que envio el servidor se considera autenticado.
 *
 * Errores:
 *   Lanza std::invalid_argument si lo > hi (error de programacion, no del
 *   servidor).
 */
RangeVerification verify_range(const Digest& root_digest, Key lo, Key hi, const RangeProof& proof);

}  // namespace tt::ait

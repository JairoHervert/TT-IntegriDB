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

enum class EntryKind : std::uint8_t { kCover = 0, kExpanded = 1, kPruned = 2 };

struct ProofEntry {
    EntryKind kind = EntryKind::kPruned;
    NodeHeader header;       // kCover, kExpanded
    NodePayload payload;     // kCover
    Digest payload_digest{}; // kExpanded
    Digest left_hash{};      // kCover interno
    Digest right_hash{};     // kCover interno
    Digest hash{};           // kPruned
};

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

struct RangeVerification {
    bool accepted = false;
    std::string reason;             // por que se rechazo (vacio si se acepto)
    std::vector<CoverNode> cover;   // en orden de clave
    std::uint64_t count = 0;        // filas en el rango (suma de los contadores)
};

// Verifica la prueba de RangeCover(lo, hi) contra el digest vigente del AIT.
// No usa emparejamientos: solo hashes. Lanza std::invalid_argument si lo > hi.
RangeVerification verify_range(const Digest& root_digest, Key lo, Key hi, const RangeProof& proof);

}  // namespace tt::ait

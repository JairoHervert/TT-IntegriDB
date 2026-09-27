// tree.hpp
//
// AIT completo del lado del servidor (ait^T_{i x j} del reporte) y su
// construccion por el Propietario de datos (AITSetup).
//
// Setup construye un arbol perfectamente balanceado a partir de las hojas
// ordenadas (el subarbol izquierdo recibe la mitad superior redondeada),
// sin rotaciones. Con las mismas filas y las mismas claves el resultado es
// siempre el mismo arbol y el mismo digest. Las actualizaciones (Insert,
// Delete con rotaciones de arbol rojinegro) se agregaran despues sobre esta
// misma estructura de nodos.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tt/ait/node.hpp"
#include "tt/ait/range_proof.hpp"
#include "tt/scheme/keys.hpp"

namespace tt::ait {

struct Node {
    NodeHeader header;
    NodePayload payload;
    Digest payload_digest{};
    Digest hash{};
    std::int32_t left = -1;     // indice del hijo izquierdo en Tree::nodes()
    std::int32_t right = -1;
    std::vector<Value> values;  // solo hojas: valores de la clave, en orden ascendente
};

class Tree {
public:
    Tree();  // arbol vacio

    // Digest delta del AIT: hash de la raiz (compromete todo el arbol).
    const Digest& root_digest() const { return root_digest_; }

    bool empty() const { return root_ < 0; }
    std::size_t leaf_count() const { return leaf_count_; }    // claves distintas
    std::size_t value_count() const { return value_count_; }  // filas
    std::size_t node_count() const { return nodes_.size(); }
    std::int32_t root() const { return root_; }
    const std::vector<Node>& nodes() const { return nodes_; }

    // RangeCover(lo, hi) del lado del probador: valores en el rango y la
    // prueba. Lanza std::invalid_argument si lo > hi.
    RangeAnswer range(Key lo, Key hi) const;

    // Search(k) del reporte: RangeCover(k, k).
    RangeAnswer search(Key key) const { return range(key, key); }

private:
    friend Tree setup(std::vector<Entry> entries, const scheme::SecretKey& sk);

    void emit(std::int32_t index, Key lo, Key hi, RangeAnswer& answer) const;
    void collect_values(std::int32_t index, std::vector<Value>& out) const;

    std::vector<Node> nodes_;
    std::int32_t root_ = -1;
    Digest root_digest_{};
    std::size_t leaf_count_ = 0;
    std::size_t value_count_ = 0;
};

// AITSetup_{f_{s,sk}}(S, bottom) del reporte. Solo el propietario puede
// ejecutarlo: necesita s para los acumuladores y sk para Enc.
// Lanza std::invalid_argument si algun valor es 0.
Tree setup(std::vector<Entry> entries, const scheme::SecretKey& sk);

// Recupera e_u de un nodo descifrando Enc_sk(e_u). Lo usa el propietario en
// las actualizaciones. Lanza crypto::DecryptionError si el cifrado fue
// alterado.
NTL::ZZ_p decrypt_exponent(const NodePayload& payload, const scheme::SecretKey& sk);

}  // namespace tt::ait

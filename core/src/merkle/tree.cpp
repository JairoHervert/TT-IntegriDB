// tree.cpp

#include "tt/merkle/tree.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace tt::merkle {

namespace {

constexpr std::uint8_t kTagNode = 0x11;
constexpr std::uint8_t kTagEmpty = 0x12;

}  // namespace

/*
 * Implementacion: sin niveles; la raiz es la del arbol vacio.
 */
Tree::Tree() : root_(empty_root()) {}

/*
 * Implementacion: el nivel 0 son las hojas; cada nivel siguiente empareja
 * nodos vecinos (0 con 1, 2 con 3, ...) y, si sobra uno al final, lo sube
 * sin cambios. Termina cuando un nivel tiene un solo nodo: la raiz.
 */
Tree::Tree(std::vector<Digest> leaves) {
    if (leaves.empty()) {
        root_ = empty_root();
        return;
    }
    levels_.push_back(std::move(leaves));
    while (levels_.back().size() > 1) {
        const std::vector<Digest>& below = levels_.back();
        std::vector<Digest> above;
        above.reserve((below.size() + 1) / 2);
        for (std::size_t i = 0; i < below.size(); i += 2) {
            above.push_back(i + 1 < below.size() ? node_hash(below[i], below[i + 1]) : below[i]);
        }
        levels_.push_back(std::move(above));
    }
    root_ = levels_.back().front();
}

/*
 * Implementacion: devuelve el nivel 0 (una lista vacia estatica si no hay
 * hojas).
 */
const std::vector<Digest>& Tree::leaves() const {
    static const std::vector<Digest> kNone;
    return levels_.empty() ? kNone : levels_.front();
}

/*
 * Implementacion: sube nivel por nivel desde la hoja. Si la posicion es
 * impar, el hermano esta a la izquierda; si es par y existe un nodo a la
 * derecha, ese es el hermano; si es el ultimo nodo de un nivel impar, sube
 * sin hermano y no se agrega nada a la prueba.
 */
InclusionProof Tree::prove(std::uint64_t index) const {
    if (index >= size()) {
        throw std::out_of_range("Merkle: posicion " + std::to_string(index) + " fuera del arbol de " +
                                std::to_string(size()) + " hojas");
    }
    InclusionProof proof;
    proof.index = index;
    std::uint64_t i = index;
    for (std::size_t level = 0; level + 1 < levels_.size(); ++level) {
        const std::vector<Digest>& nodes = levels_[level];
        if (i % 2 == 1) {
            proof.path.push_back(nodes[static_cast<std::size_t>(i - 1)]);
        } else if (i + 1 < nodes.size()) {
            proof.path.push_back(nodes[static_cast<std::size_t>(i + 1)]);
        }
        i /= 2;
    }
    return proof;
}

/*
 * Implementacion: SHA-256 con la etiqueta 0x11 y los dos hijos en orden.
 */
Digest node_hash(const Digest& left, const Digest& right) {
    crypto::Sha256 h;
    h.u8(kTagNode).digest(left).digest(right);
    return h.finish();
}

/*
 * Implementacion: SHA-256 de un solo byte, la etiqueta 0x12.
 */
Digest empty_root() {
    crypto::Sha256 h;
    h.u8(kTagEmpty);
    return h.finish();
}

/*
 * Implementacion: algoritmo de RFC 9162, seccion 2.1.3.2. fn es la
 * posicion del nodo actual y sn la del ultimo nodo de su nivel. Si el nodo
 * es hijo derecho (fn impar) o es el ultimo de su nivel, el hermano va a la
 * izquierda; en el segundo caso, ademas, se saltan los niveles en los que
 * el nodo sube sin hermano. Si no, el hermano va a la derecha. Al final
 * deben haberse usado exactamente todos los hermanos (sn == 0) y la raiz
 * recalculada debe coincidir.
 */
bool verify_inclusion(const Digest& root, std::uint64_t size, const Digest& leaf,
                      const InclusionProof& proof) {
    if (proof.index >= size) return false;
    std::uint64_t fn = proof.index;
    std::uint64_t sn = size - 1;
    Digest r = leaf;
    for (const Digest& p : proof.path) {
        if (sn == 0) return false;  // sobran hermanos
        if ((fn & 1) == 1 || fn == sn) {
            r = node_hash(p, r);
            if ((fn & 1) == 0) {
                while ((fn & 1) == 0 && fn != 0) {
                    fn >>= 1;
                    sn >>= 1;
                }
            }
        } else {
            r = node_hash(r, p);
        }
        fn >>= 1;
        sn >>= 1;
    }
    return sn == 0 && r == root;
}

}  // namespace tt::merkle

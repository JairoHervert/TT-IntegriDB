// range_proof.cpp
//
// Verificacion de una prueba de RangeCover (lado del verificador).

#include "tt/ait/range_proof.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace tt::ait {

namespace {

// Profundidad maxima aceptada. Un arbol balanceado con 2^20 hojas tiene
// profundidad 21 y uno rojinegro a lo mas el doble; el limite evita que una
// prueba maliciosa con miles de nodos anidados agote la pila.
constexpr int kMaxDepth = 128;

class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProofChecker {
public:
    ProofChecker(const RangeProof& proof, Key lo, Key hi, RangeVerification& out)
        : proof_(proof), lo_(lo), hi_(hi), out_(out) {}

    // Recalcula el hash del subarbol que empieza en la siguiente entrada.
    Digest node(int depth) {
        if (depth > kMaxDepth) throw Rejected("la prueba es mas profunda que cualquier AIT valido");
        const ProofEntry& e = next();
        if (e.kind == EntryKind::kPruned) {
            throw Rejected("hay un subarbol podado donde las claves no demuestran que este fuera del "
                           "rango: podria esconder filas del resultado");
        }
        const NodeHeader& h = e.header;
        check_shape(h);
        const bool inside = fully_inside(h, lo_, hi_);

        if (e.kind == EntryKind::kCover) {
            if (!inside) throw Rejected("un nodo de cobertura contiene claves fuera del rango");
            if (out_.count > std::numeric_limits<std::uint64_t>::max() - h.count) {
                throw Rejected("el total de filas desborda");
            }
            const Digest pd = payload_hash(e.payload);
            const Digest hash =
                h.is_leaf ? leaf_hash(h, pd) : internal_hash(h, pd, e.left_hash, e.right_hash);
            out_.cover.push_back(CoverNode{h, e.payload});
            out_.count += h.count;
            return hash;
        }

        if (e.kind != EntryKind::kExpanded) throw Rejected("entrada de tipo desconocido");
        if (inside) {
            throw Rejected("un nodo completo dentro del rango aparece expandido en lugar de como "
                           "nodo de cobertura");
        }
        if (h.is_leaf) return leaf_hash(h, e.payload_digest);

        const Digest left = left_child_outside(h, lo_, hi_) ? pruned() : node(depth + 1);
        const Digest right = right_child_outside(h, lo_, hi_) ? pruned() : node(depth + 1);
        return internal_hash(h, e.payload_digest, left, right);
    }

    bool consumed_all() const { return pos_ == proof_.entries.size(); }

private:
    const ProofEntry& next() {
        if (pos_ >= proof_.entries.size()) throw Rejected("la prueba termina antes de tiempo");
        return proof_.entries[pos_++];
    }

    Digest pruned() {
        const ProofEntry& e = next();
        if (e.kind != EntryKind::kPruned) {
            throw Rejected("se esperaba un subarbol podado: las claves demuestran que esta fuera del "
                           "rango");
        }
        return e.hash;
    }

    // Forma basica del encabezado. El hash ya lo autentica; esto solo
    // rechaza antes y con un mensaje claro los encabezados imposibles.
    static void check_shape(const NodeHeader& h) {
        if (h.count == 0) throw Rejected("nodo sin filas");
        if (h.is_leaf) {
            if (h.min_key != h.split_key || h.max_key != h.split_key) {
                throw Rejected("hoja con claves inconsistentes");
            }
        } else if (!(h.min_key <= h.split_key && h.split_key < h.max_key) || h.count < 2) {
            throw Rejected("nodo interno con claves inconsistentes");
        }
    }

    const RangeProof& proof_;
    Key lo_;
    Key hi_;
    RangeVerification& out_;
    std::size_t pos_ = 0;
};

}  // namespace

RangeVerification verify_range(const Digest& root_digest, Key lo, Key hi, const RangeProof& proof) {
    if (lo > hi) throw std::invalid_argument("AIT: rango invalido, lo > hi");

    RangeVerification out;
    try {
        Digest computed;
        if (proof.entries.empty()) {
            computed = empty_tree_hash();  // arbol sin filas: resultado vacio
        } else {
            ProofChecker checker(proof, lo, hi, out);
            computed = checker.node(0);
            if (!checker.consumed_all()) throw Rejected("la prueba tiene entradas de sobra");
        }
        if (computed != root_digest) {
            throw Rejected("la prueba no corresponde al digest vigente del AIT");
        }
        out.accepted = true;
    } catch (const Rejected& r) {
        out = RangeVerification{};  // nada de lo leido queda como autenticado
        out.reason = r.what();
    }
    return out;
}

}  // namespace tt::ait

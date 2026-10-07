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

/*
 * Excepcion interna para abandonar la verificacion en cuanto algo no cuadra.
 * verify_range la atrapa y la convierte en un rechazo con su motivo.
 */
class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Recorre una prueba de RangeCover en preorden, recalculando los hashes de
 * abajo hacia arriba y aplicando las reglas de forma canonica. Guarda en
 * `out` los nodos de cobertura que va encontrando.
 */
class ProofChecker {
public:
    /*
     * Prepara la revision de `proof` para el rango [lo, hi].
     */
    ProofChecker(const RangeProof& proof, Key lo, Key hi, RangeVerification& out)
        : proof_(proof), lo_(lo), hi_(hi), out_(out) {}

    /*
     * Lee el subarbol que empieza en la siguiente entrada y devuelve su hash
     * recalculado. Aplica las reglas:
     *   - un nodo no puede venir podado si su padre no justifico la poda;
     *   - un nodo de cobertura debe estar completo dentro del rango;
     *   - un nodo completo dentro del rango debe venir como cobertura;
     *   - en un nodo expandido, cada hijo se poda si y solo si las reglas de
     *     node.hpp lo justifican.
     *
     * Parametros:
     *   depth  profundidad actual, para rechazar pruebas anidadas sin fin
     *
     * Devuelve:
     *   El hash del subarbol.
     *
     * Errores:
     *   Lanza Rejected si la prueba viola alguna regla.
     */
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

    /*
     * Indica si ya se leyeron todas las entradas (una prueba valida no deja
     * entradas sin usar).
     */
    bool consumed_all() const { return pos_ == proof_.entries.size(); }

private:
    /*
     * Devuelve la siguiente entrada de la prueba y avanza.
     *
     * Errores:
     *   Lanza Rejected si la prueba ya no tiene mas entradas.
     */
    const ProofEntry& next() {
        if (pos_ >= proof_.entries.size()) throw Rejected("la prueba termina antes de tiempo");
        return proof_.entries[pos_++];
    }

    /*
     * Lee un subarbol que debe venir podado (el padre demostro que esta fuera
     * del rango) y devuelve su hash.
     *
     * Errores:
     *   Lanza Rejected si la entrada no es de tipo podado.
     */
    Digest pruned() {
        const ProofEntry& e = next();
        if (e.kind != EntryKind::kPruned) {
            throw Rejected("se esperaba un subarbol podado: las claves demuestran que esta fuera del "
                           "rango");
        }
        return e.hash;
    }

    /*
     * Revisa la forma basica de un encabezado: una hoja tiene min = max =
     * clave; un nodo interno cumple min <= clave < max y tiene al menos dos
     * filas. El hash ya autentica el encabezado; esto solo rechaza antes, y con
     * un mensaje claro, los encabezados imposibles.
     *
     * Errores:
     *   Lanza Rejected si el encabezado no tiene una forma valida.
     */
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

/*
 * Implementacion: una prueba vacia solo vale para el arbol vacio; si no,
 * ProofChecker recalcula el hash de la raiz, se exige que no sobren entradas
 * y que el hash coincida con el digest. Ante cualquier rechazo se descarta
 * todo lo leido y se guarda el motivo.
 */
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

// tree.cpp

#include "tt/ait/tree.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "tt/crypto/pairing_group.hpp"
#include "tt/crypto/symmetric.hpp"

namespace tt::ait {

namespace {

// Todas las filas con la misma clave van a una sola hoja.
struct KeyGroup {
    Key key;
    std::vector<Value> values;
};

/*
 * Construye los nodos de un AIT de abajo hacia arriba. Existe solo durante
 * setup(): guarda referencias a los datos que necesita cada paso para no
 * pasarlos como argumentos en cada llamada recursiva.
 */
class Builder {
public:
    /*
     * Prepara el constructor.
     *
     * Parametros:
     *   nodes   vector donde se van agregando los nodos del arbol
     *   groups  hojas ya ordenadas por clave (una por clave distinta)
     *   sk      clave secreta del propietario (s y la clave de Enc)
     *   group   generadores g1 y g2
     */
    Builder(std::vector<Node>& nodes, const std::vector<KeyGroup>& groups, const scheme::SecretKey& sk,
            const crypto::PairingGroup& group)
        : nodes_(nodes), groups_(groups), sk_(sk), group_(group) {}

    /*
     * Construye el subarbol con las hojas [lo, hi) y devuelve el indice de su
     * raiz en `nodes`.
     *
     * Si hay una sola hoja, calcula su exponente como el producto de
     * (v^{-1} + s) sobre sus valores. Si hay varias, divide las hojas en dos
     * mitades (la izquierda con la mitad redondeada hacia arriba, lo que
     * reproduce los arboles de las figuras del reporte), construye cada mitad y
     * combina: exponente = producto de los de sus hijos; clave = maxima del
     * hijo izquierdo; minima y maxima del subarbol; contador = suma.
     *
     * Parametros:
     *   lo, hi    rango de hojas (indices de groups)
     *   exponent  recibe e_u del subarbol; lo necesita el padre y nunca se
     *             guarda en claro en el nodo
     *
     * Devuelve:
     *   Indice del nodo creado.
     */
    std::int32_t build(std::size_t lo, std::size_t hi, NTL::ZZ_p& exponent) {
        Node node;
        if (hi - lo == 1) {
            const KeyGroup& g = groups_[lo];
            exponent = 1;
            for (Value v : g.values) exponent *= value_to_element(v) + sk_.aso.s;

            node.header.is_leaf = true;
            node.header.min_key = node.header.max_key = node.header.split_key = g.key;
            node.header.count = g.values.size();
            node.values = g.values;
            node.payload = make_payload(exponent);
            node.payload_digest = payload_hash(node.payload);
            node.hash = leaf_hash(node.header, node.payload_digest);
        } else {
            // El subarbol izquierdo recibe la mitad superior redondeada: con
            // 8 hojas da exactamente los arboles de las figuras del reporte.
            const std::size_t mid = lo + (hi - lo + 1) / 2;
            NTL::ZZ_p left_exponent;
            NTL::ZZ_p right_exponent;
            const std::int32_t left = build(lo, mid, left_exponent);
            const std::int32_t right = build(mid, hi, right_exponent);
            exponent = left_exponent * right_exponent;

            const NodeHeader& lh = nodes_[static_cast<std::size_t>(left)].header;
            const NodeHeader& rh = nodes_[static_cast<std::size_t>(right)].header;
            node.header.is_leaf = false;
            node.header.min_key = lh.min_key;
            node.header.max_key = rh.max_key;
            node.header.split_key = lh.max_key;
            node.header.count = lh.count + rh.count;
            node.left = left;
            node.right = right;
            node.payload = make_payload(exponent);
            node.payload_digest = payload_hash(node.payload);
            node.hash = internal_hash(node.header, node.payload_digest,
                                      nodes_[static_cast<std::size_t>(left)].hash,
                                      nodes_[static_cast<std::size_t>(right)].hash);
        }
        nodes_.push_back(std::move(node));
        return static_cast<std::int32_t>(nodes_.size() - 1);
    }

private:
    /*
     * Calcula el valor v_u de un nodo a partir de su exponente e_u:
     * g1^{e_u}, g2^{e_u} y Enc_sk(e_u). El exponente en bytes se borra de
     * memoria despues de cifrarlo.
     */
    NodePayload make_payload(const NTL::ZZ_p& exponent) const {
        NodePayload payload;
        const mie::Vuint e = crypto::to_vuint(exponent);
        payload.acc_g1 = group_.g1 * e;
        payload.acc_g2 = group_.g2 * e;
        std::vector<unsigned char> plain = crypto::scalar_to_bytes(exponent);
        payload.enc = crypto::encrypt_deterministic(sk_.enc_key, plain);
        OPENSSL_cleanse(plain.data(), plain.size());
        return payload;
    }

    std::vector<Node>& nodes_;
    const std::vector<KeyGroup>& groups_;
    const scheme::SecretKey& sk_;
    const crypto::PairingGroup& group_;
};

}  // namespace

/*
 * Implementacion: sin nodos; el digest es el de un arbol vacio.
 */
Tree::Tree() : root_digest_(empty_tree_hash()) {}

/*
 * Implementacion: valida que no haya valores 0, ordena las filas por
 * (clave, valor), las agrupa por clave y deja que Builder construya el
 * arbol. El digest del arbol es el hash de su raiz.
 */
Tree setup(std::vector<Entry> entries, const scheme::SecretKey& sk) {
    const crypto::PairingGroup& group = crypto::init_pairing_group();

    for (const Entry& e : entries) {
        if (e.value == 0) {
            throw std::invalid_argument("AIT: la columna valor contiene 0, que no tiene inverso (clave " +
                                        std::to_string(e.key) + ")");
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.key < b.key || (a.key == b.key && a.value < b.value);
    });

    std::vector<KeyGroup> groups;
    for (const Entry& e : entries) {
        if (groups.empty() || groups.back().key != e.key) groups.push_back({e.key, {}});
        groups.back().values.push_back(e.value);
    }

    Tree tree;
    if (groups.empty()) return tree;

    tree.nodes_.reserve(2 * groups.size() - 1);
    Builder builder(tree.nodes_, groups, sk, group);
    NTL::ZZ_p root_exponent;
    tree.root_ = builder.build(0, groups.size(), root_exponent);
    tree.root_digest_ = tree.nodes_[static_cast<std::size_t>(tree.root_)].hash;
    tree.leaf_count_ = groups.size();
    tree.value_count_ = entries.size();
    return tree;
}

/*
 * Implementacion: valida el rango y recorre el arbol desde la raiz con
 * emit(). Un arbol vacio responde con una prueba vacia.
 */
RangeAnswer Tree::range(Key lo, Key hi) const {
    if (lo > hi) throw std::invalid_argument("AIT: rango invalido, lo > hi");
    RangeAnswer answer;
    if (root_ >= 0) emit(root_, lo, hi, answer);
    return answer;
}

/*
 * Implementacion: si el nodo esta completo dentro del rango, se agrega como
 * nodo de cobertura (con su valor y los hashes de sus hijos) y se recogen
 * sus valores. Si no, se agrega expandido (encabezado y H(v_u)) y, si es
 * interno, cada hijo se poda cuando las reglas de node.hpp lo justifican o
 * se recorre en caso contrario. Es exactamente la forma canonica que exige
 * verify_range.
 */
void Tree::emit(std::int32_t index, Key lo, Key hi, RangeAnswer& answer) const {
    const Node& node = nodes_[static_cast<std::size_t>(index)];

    if (fully_inside(node.header, lo, hi)) {
        ProofEntry entry;
        entry.kind = EntryKind::kCover;
        entry.header = node.header;
        entry.payload = node.payload;
        if (!node.header.is_leaf) {
            entry.left_hash = nodes_[static_cast<std::size_t>(node.left)].hash;
            entry.right_hash = nodes_[static_cast<std::size_t>(node.right)].hash;
        }
        answer.proof.entries.push_back(std::move(entry));
        collect_values(index, answer.values);
        return;
    }

    ProofEntry entry;
    entry.kind = EntryKind::kExpanded;
    entry.header = node.header;
    entry.payload_digest = node.payload_digest;
    answer.proof.entries.push_back(std::move(entry));
    if (node.header.is_leaf) return;

    /*
     * Agrega a la prueba el hijo indicado como subarbol podado (solo su hash).
     */
    auto pruned = [&](std::int32_t child) {
        ProofEntry p;
        p.kind = EntryKind::kPruned;
        p.hash = nodes_[static_cast<std::size_t>(child)].hash;
        answer.proof.entries.push_back(std::move(p));
    };
    if (left_child_outside(node.header, lo, hi)) {
        pruned(node.left);
    } else {
        emit(node.left, lo, hi, answer);
    }
    if (right_child_outside(node.header, lo, hi)) {
        pruned(node.right);
    } else {
        emit(node.right, lo, hi, answer);
    }
}

/*
 * Implementacion: recorrido recursivo de izquierda a derecha; cada hoja
 * aporta sus valores.
 */
void Tree::collect_values(std::int32_t index, std::vector<Value>& out) const {
    const Node& node = nodes_[static_cast<std::size_t>(index)];
    if (node.header.is_leaf) {
        out.insert(out.end(), node.values.begin(), node.values.end());
        return;
    }
    collect_values(node.left, out);
    collect_values(node.right, out);
}

/*
 * Implementacion: en tres pasos.
 *   1. Estructura: cuenta cuantos padres tiene cada nodo (la raiz ninguno,
 *      los demas exactamente uno) y recorre el arbol desde la raiz con una
 *      pila (sin recursion, para que un archivo con una cadena enorme de
 *      nodos no agote la pila); si no se visitan todos, hay nodos sueltos o
 *      ciclos.
 *   2. Contenido: recorre los nodos en preorden inverso, que pone a cada
 *      hijo antes que su padre, revisando cada encabezado contra sus hijos.
 *   3. Hashes: en ese mismo recorrido recalcula H(v_u) y el hash del nodo.
 */
Tree restore(std::vector<Node> nodes, std::int32_t root) {
    Tree tree;
    if (nodes.empty()) {
        if (root != -1) throw std::invalid_argument("AIT: raiz sin nodos");
        return tree;
    }
    const std::size_t n = nodes.size();
    if (n > static_cast<std::size_t>(INT32_MAX)) throw std::invalid_argument("AIT: demasiados nodos");
    if (root < 0 || static_cast<std::size_t>(root) >= n) throw std::invalid_argument("AIT: raiz fuera de rango");

    /*
     * Indica si un indice de hijo apunta a un nodo existente.
     */
    auto valid_index = [n](std::int32_t i) { return i >= 0 && static_cast<std::size_t>(i) < n; };

    // 1. Estructura.
    std::vector<std::uint8_t> parents(n, 0);
    for (const Node& node : nodes) {
        if (node.header.is_leaf) {
            if (node.left != -1 || node.right != -1) throw std::invalid_argument("AIT: una hoja tiene hijos");
            continue;
        }
        if (!valid_index(node.left) || !valid_index(node.right) || node.left == node.right) {
            throw std::invalid_argument("AIT: nodo interno con hijos invalidos");
        }
        for (std::int32_t child : {node.left, node.right}) {
            if (++parents[static_cast<std::size_t>(child)] > 1) {
                throw std::invalid_argument("AIT: un nodo tiene mas de un padre");
            }
        }
    }
    if (parents[static_cast<std::size_t>(root)] != 0) throw std::invalid_argument("AIT: la raiz tiene padre");
    std::vector<std::int32_t> preorder;
    preorder.reserve(n);
    std::vector<std::int32_t> stack{root};
    while (!stack.empty()) {
        const std::int32_t i = stack.back();
        stack.pop_back();
        preorder.push_back(i);
        const Node& node = nodes[static_cast<std::size_t>(i)];
        if (!node.header.is_leaf) {
            stack.push_back(node.right);
            stack.push_back(node.left);
        }
    }
    if (preorder.size() != n) throw std::invalid_argument("AIT: hay nodos que no cuelgan de la raiz");

    // 2 y 3. Contenido y hashes, de las hojas hacia la raiz.
    std::size_t leaves = 0;
    for (auto it = preorder.rbegin(); it != preorder.rend(); ++it) {
        Node& node = nodes[static_cast<std::size_t>(*it)];
        const NodeHeader& h = node.header;
        node.payload_digest = payload_hash(node.payload);
        if (h.is_leaf) {
            if (h.min_key != h.split_key || h.max_key != h.split_key) {
                throw std::invalid_argument("AIT: hoja con claves inconsistentes");
            }
            if (h.count == 0 || h.count != node.values.size()) {
                throw std::invalid_argument("AIT: el contador de una hoja no coincide con sus valores");
            }
            for (std::size_t v = 0; v < node.values.size(); ++v) {
                if (node.values[v] == 0 || (v > 0 && node.values[v] < node.values[v - 1])) {
                    throw std::invalid_argument("AIT: valores de hoja en cero o fuera de orden");
                }
            }
            node.hash = leaf_hash(h, node.payload_digest);
            ++leaves;
            continue;
        }
        const Node& l = nodes[static_cast<std::size_t>(node.left)];
        const Node& r = nodes[static_cast<std::size_t>(node.right)];
        if (!node.values.empty()) throw std::invalid_argument("AIT: un nodo interno trae valores");
        if (h.min_key != l.header.min_key || h.max_key != r.header.max_key || h.split_key != l.header.max_key ||
            !(l.header.max_key < r.header.min_key)) {
            throw std::invalid_argument("AIT: las claves de un nodo interno no coinciden con sus hijos");
        }
        if (l.header.count > UINT64_MAX - r.header.count || h.count != l.header.count + r.header.count) {
            throw std::invalid_argument("AIT: el contador de un nodo interno no es la suma de sus hijos");
        }
        node.hash = internal_hash(h, node.payload_digest, l.hash, r.hash);
    }

    tree.nodes_ = std::move(nodes);
    tree.root_ = root;
    tree.root_digest_ = tree.nodes_[static_cast<std::size_t>(root)].hash;
    tree.leaf_count_ = leaves;
    tree.value_count_ = static_cast<std::size_t>(tree.nodes_[static_cast<std::size_t>(root)].header.count);
    return tree;
}

/*
 * Implementacion: descifra con decrypt_deterministic (que ya detecta
 * alteraciones), convierte los 32 bytes en elemento de Z_r y borra el texto
 * descifrado de memoria.
 */
NTL::ZZ_p decrypt_exponent(const NodePayload& payload, const scheme::SecretKey& sk) {
    std::vector<unsigned char> plain = crypto::decrypt_deterministic(sk.enc_key, payload.enc);
    try {
        NTL::ZZ_p exponent = crypto::scalar_from_bytes(plain);
        OPENSSL_cleanse(plain.data(), plain.size());
        return exponent;
    } catch (const std::invalid_argument& e) {
        OPENSSL_cleanse(plain.data(), plain.size());
        throw crypto::DecryptionError(std::string("el exponente descifrado no es valido: ") + e.what());
    }
}

}  // namespace tt::ait

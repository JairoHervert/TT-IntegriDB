// tree.cpp

#include "tt/ait/tree.hpp"

#include <openssl/crypto.h>

#include <algorithm>
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

class Builder {
public:
    Builder(std::vector<Node>& nodes, const std::vector<KeyGroup>& groups, const scheme::SecretKey& sk,
            const crypto::PairingGroup& group)
        : nodes_(nodes), groups_(groups), sk_(sk), group_(group) {}

    // Construye el subarbol con las hojas [lo, hi) y devuelve su indice.
    // `exponent` recibe e_u = prod (v^{-1} + s) del subarbol; nunca se
    // guarda en claro en el nodo.
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

Tree::Tree() : root_digest_(empty_tree_hash()) {}

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

RangeAnswer Tree::range(Key lo, Key hi) const {
    if (lo > hi) throw std::invalid_argument("AIT: rango invalido, lo > hi");
    RangeAnswer answer;
    if (root_ >= 0) emit(root_, lo, hi, answer);
    return answer;
}

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

void Tree::collect_values(std::int32_t index, std::vector<Value>& out) const {
    const Node& node = nodes_[static_cast<std::size_t>(index)];
    if (node.header.is_leaf) {
        out.insert(out.end(), node.values.begin(), node.values.end());
        return;
    }
    collect_values(node.left, out);
    collect_values(node.right, out);
}

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

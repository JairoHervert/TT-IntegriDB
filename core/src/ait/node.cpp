// node.cpp

#include "tt/ait/node.hpp"

#include <stdexcept>

#include "tt/aso/accumulator.hpp"

namespace tt::ait {

namespace {

// Etiquetas de dominio: cada tipo de hash empieza con un byte distinto.
constexpr std::uint8_t kTagLeaf = 0x00;
constexpr std::uint8_t kTagInternal = 0x01;
constexpr std::uint8_t kTagPayload = 0x02;
constexpr std::uint8_t kTagEmptyTree = 0x03;

}  // namespace

Digest payload_hash(const NodePayload& payload) {
    crypto::Sha256 h;
    h.u8(kTagPayload).point(payload.acc_g1).point(payload.acc_g2).length_prefixed(payload.enc);
    return h.finish();
}

Digest leaf_hash(const NodeHeader& header, const Digest& payload_digest) {
    crypto::Sha256 h;
    h.u8(kTagLeaf).i64(header.split_key).u64(header.count).digest(payload_digest);
    return h.finish();
}

Digest internal_hash(const NodeHeader& header, const Digest& payload_digest, const Digest& left,
                     const Digest& right) {
    crypto::Sha256 h;
    h.u8(kTagInternal)
        .i64(header.min_key)
        .i64(header.max_key)
        .i64(header.split_key)
        .u64(header.count)
        .digest(payload_digest)
        .digest(left)
        .digest(right);
    return h.finish();
}

Digest empty_tree_hash() {
    crypto::Sha256 h;
    h.u8(kTagEmptyTree);
    return h.finish();
}

NTL::ZZ_p value_to_element(Value v) {
    if (v == 0) {
        throw std::invalid_argument("AIT: el valor 0 no tiene inverso; la columna valor no puede contener 0");
    }
    return NTL::inv(aso::to_field(v));
}

bool fully_inside(const NodeHeader& h, Key lo, Key hi) { return lo <= h.min_key && h.max_key <= hi; }

bool left_child_outside(const NodeHeader& h, Key lo, Key hi) {
    return h.split_key < lo || h.min_key > hi;
}

bool right_child_outside(const NodeHeader& h, Key lo, Key hi) {
    return h.split_key >= hi || h.max_key < lo;
}

}  // namespace tt::ait

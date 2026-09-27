// group_ops.hpp
//
// Operaciones genericas sobre puntos de G1 o G2 que usan varios bloques
// del esquema (ASO, AIT, pruebas).

#pragma once

#include <NTL/ZZ_p.h>
#include <NTL/vec_ZZ_p.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "bn.h"
#include "tt/crypto/pairing_group.hpp"

namespace tt::crypto {

// Punto al infinito (neutro de la suma) de G1 o G2.
template <class Point>
Point zero_point() {
    Point z;
    z.clear();
    return z;
}

// sum_{i < count} scalars[i] * bases[offset + i]
//
// Implementacion directa: una multiplicacion escalar por termino, saltando
// los escalares 0 y 1. (Si mas adelante el probador necesita velocidad, aqui
// es donde entraria un algoritmo de multi-exponenciacion como Pippenger.)
template <class Point>
Point linear_combination(const std::vector<Point>& bases, std::size_t offset,
                         const NTL::ZZ_p* scalars, std::size_t count) {
    if (offset > bases.size() || count > bases.size() - offset) {
        throw std::out_of_range("linear_combination: hay mas escalares que bases");
    }
    Point acc = zero_point<Point>();
    for (std::size_t i = 0; i < count; ++i) {
        const NTL::ZZ_p& c = scalars[i];
        if (NTL::IsZero(c)) continue;
        if (NTL::IsOne(c)) {
            acc = acc + bases[offset + i];
        } else {
            acc = acc + bases[offset + i] * to_vuint(c);
        }
    }
    return acc;
}

template <class Point>
Point linear_combination(const std::vector<Point>& bases, std::size_t offset,
                         const std::vector<NTL::ZZ_p>& scalars) {
    return linear_combination(bases, offset, scalars.data(), scalars.size());
}

template <class Point>
Point linear_combination(const std::vector<Point>& bases, std::size_t offset,
                         const NTL::vec_ZZ_p& scalars) {
    return linear_combination(bases, offset, scalars.elts(),
                              static_cast<std::size_t>(scalars.length()));
}

// true si P * r = O, es decir, si P esta en el subgrupo de orden r.
// En G1 (cofactor 1) todo punto de la curva lo cumple; en G2 no, y hay que
// revisarlo para los puntos que usa el verificador.
template <class Point>
bool in_prime_order_subgroup(const Point& p) {
    return (p * bn::Param::r).isZero();
}

}  // namespace tt::crypto

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

/*
 * Devuelve el punto al infinito O (el neutro de la suma) de G1 o de G2,
 * segun el tipo de punto que se pida: zero_point<bn::Ec1>() o
 * zero_point<bn::Ec2>(). Sirve como valor inicial de una suma de puntos.
 */
template <class Point>
Point zero_point() {
    Point z;
    z.clear();
    return z;
}

/*
 * Calcula la combinacion lineal
 *
 *     scalars[0] * bases[offset] + ... + scalars[count-1] * bases[offset + count - 1]
 *
 * Es la operacion con la que el probador calcula un acumulador sin conocer
 * s: combina las potencias publicas g^{s^i} con los coeficientes del
 * polinomio caracteristico del conjunto.
 *
 * Hace una multiplicacion escalar por termino y se salta los coeficientes 0
 * y 1. Si mas adelante el probador necesita velocidad, aqui es donde
 * entraria un algoritmo de multi-exponenciacion como Pippenger.
 *
 * Parametros:
 *   bases    puntos de G1 o G2
 *   offset   indice de la primera base que se usa
 *   scalars  coeficientes en Z_r
 *   count    cuantos coeficientes usar
 *
 * Devuelve:
 *   El punto resultante (O si count es 0).
 *
 * Errores:
 *   Lanza std::out_of_range si no hay suficientes bases a partir de offset.
 */
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

/*
 * Igual que la version anterior, pero toma todos los coeficientes de un
 * std::vector.
 */
template <class Point>
Point linear_combination(const std::vector<Point>& bases, std::size_t offset,
                         const std::vector<NTL::ZZ_p>& scalars) {
    return linear_combination(bases, offset, scalars.data(), scalars.size());
}

/*
 * Igual que la version anterior, pero toma los coeficientes de un vector de
 * NTL. Es la forma natural de pasar los coeficientes de un polinomio
 * (NTL::ZZ_pX::rep).
 */
template <class Point>
Point linear_combination(const std::vector<Point>& bases, std::size_t offset,
                         const NTL::vec_ZZ_p& scalars) {
    return linear_combination(bases, offset, scalars.elts(),
                              static_cast<std::size_t>(scalars.length()));
}

/*
 * Comprueba si un punto pertenece al subgrupo de orden r, es decir, si
 * P * r = O.
 *
 * En G1 (cofactor 1) todo punto de la curva lo cumple; en G2 no, y hay que
 * revisarlo en los puntos de G2 que usa el verificador, porque un punto
 * fuera del subgrupo podria hacer que un emparejamiento se comporte de forma
 * inesperada.
 *
 * Parametros:
 *   p  punto de G1 o G2 (ya validado como punto de su curva)
 *
 * Devuelve:
 *   true si P * r = O.
 */
template <class Point>
bool in_prime_order_subgroup(const Point& p) {
    return (p * bn::Param::r).isZero();
}

}  // namespace tt::crypto

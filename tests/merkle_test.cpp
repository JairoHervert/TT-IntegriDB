// merkle_test.cpp
//
// Pruebas del arbol de Merkle de filas: la raiz coincide con la definicion
// recursiva de RFC 6962 para cualquier numero de hojas, toda prueba
// honesta se acepta y cualquier alteracion se rechaza.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/merkle/tree.hpp"

namespace {

using tt::merkle::Digest;
using tt::merkle::InclusionProof;

/*
 * Hoja de prueba distinta para cada numero: SHA-256 de "hoja" y el numero.
 */
Digest leaf(std::uint64_t i) {
    tt::crypto::Sha256 h;
    h.bytes("hoja", 4).u64(i);
    return h.finish();
}

/*
 * Raiz segun la definicion recursiva de RFC 6962 (MTH): con n > 1 hojas,
 * k es la mayor potencia de dos menor que n y la raiz es
 * H(MTH(hojas[0, k)) || MTH(hojas[k, n))). Es una implementacion
 * independiente de la del modulo, para compararlas.
 */
Digest reference_root(const std::vector<Digest>& leaves, std::size_t lo, std::size_t hi) {
    const std::size_t n = hi - lo;
    if (n == 0) return tt::merkle::empty_root();
    if (n == 1) return leaves[lo];
    std::size_t k = 1;
    while (k * 2 < n) k *= 2;
    return tt::merkle::node_hash(reference_root(leaves, lo, lo + k), reference_root(leaves, lo + k, hi));
}

/*
 * Lista de n hojas distintas.
 */
std::vector<Digest> make_leaves(std::size_t n) {
    std::vector<Digest> leaves;
    for (std::size_t i = 0; i < n; ++i) leaves.push_back(leaf(i));
    return leaves;
}

}  // namespace

/*
 * Pruebas del arbol de Merkle: raiz contra la definicion de RFC 6962,
 * pruebas de inclusion honestas para todas las posiciones de arboles de 1
 * a 70 hojas, y rechazo de hojas, posiciones, tamanos y caminos alterados.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    using tt::test::check;
    using tt::test::section;

    section("Raiz");
    {
        check(tt::merkle::Tree().root() == tt::merkle::empty_root() && tt::merkle::Tree().size() == 0,
              "el arbol vacio tiene la raiz del arbol vacio");
        check(tt::merkle::Tree({leaf(7)}).root() == leaf(7), "con una sola hoja, la raiz es la hoja");
        bool same = true;
        for (std::size_t n = 0; n <= 70; ++n) {
            const std::vector<Digest> leaves = make_leaves(n);
            same = same && tt::merkle::Tree(leaves).root() == reference_root(leaves, 0, n);
        }
        check(same, "la raiz coincide con la definicion recursiva de RFC 6962 para 0 a 70 hojas");
        std::vector<Digest> a = make_leaves(5);
        std::vector<Digest> b = a;
        std::swap(b[1], b[2]);
        check(!(tt::merkle::Tree(a).root() == tt::merkle::Tree(b).root()), "cambiar el orden de las hojas cambia la raiz");
        std::vector<Digest> c = make_leaves(4);
        c.push_back(c.back());
        check(!(tt::merkle::Tree(make_leaves(4)).root() == tt::merkle::Tree(c).root()),
              "repetir la ultima hoja cambia la raiz (sin la ambiguedad de duplicar hojas)");
    }

    section("Pruebas de inclusion honestas");
    {
        bool all_ok = true;
        std::size_t longest = 0;
        for (std::size_t n = 1; n <= 70; ++n) {
            const tt::merkle::Tree tree(make_leaves(n));
            for (std::uint64_t i = 0; i < n; ++i) {
                const InclusionProof p = tree.prove(i);
                all_ok = all_ok && tt::merkle::verify_inclusion(tree.root(), n, leaf(i), p);
                longest = std::max(longest, p.path.size());
            }
        }
        check(all_ok, "todas las hojas de arboles de 1 a 70 hojas se verifican");
        check(longest == 7, "el camino mas largo con 70 hojas tiene ceil(log2 70) = 7 hashes");
        tt::test::check_throws<std::out_of_range>([] { tt::merkle::Tree(make_leaves(3)).prove(3); },
                                                  "pedir la prueba de una hoja que no existe lanza un error");
    }

    section("Alteraciones");
    {
        bool leaf_rejected = true, index_rejected = true, size_rejected = true;
        bool path_rejected = true, short_rejected = true, long_rejected = true;
        for (std::size_t n = 2; n <= 40; ++n) {
            const tt::merkle::Tree tree(make_leaves(n));
            for (std::uint64_t i = 0; i < n; ++i) {
                const InclusionProof p = tree.prove(i);
                leaf_rejected = leaf_rejected && !tt::merkle::verify_inclusion(tree.root(), n, leaf(i + 1000), p);
                InclusionProof moved = p;
                moved.index = (i + 1) % n;
                index_rejected = index_rejected && !tt::merkle::verify_inclusion(tree.root(), n, leaf(i), moved);
                size_rejected = size_rejected && !tt::merkle::verify_inclusion(tree.root(), i, leaf(i), p);
                for (std::size_t k = 0; k < p.path.size(); ++k) {
                    InclusionProof bad = p;
                    bad.path[k][0] ^= 1;
                    path_rejected = path_rejected && !tt::merkle::verify_inclusion(tree.root(), n, leaf(i), bad);
                }
                InclusionProof shorter = p;
                shorter.path.pop_back();
                short_rejected = short_rejected && !tt::merkle::verify_inclusion(tree.root(), n, leaf(i), shorter);
                InclusionProof longer = p;
                longer.path.push_back(leaf(0));
                long_rejected = long_rejected && !tt::merkle::verify_inclusion(tree.root(), n, leaf(i), longer);
            }
        }
        check(leaf_rejected, "una hoja distinta se rechaza");
        check(index_rejected, "la prueba de una posicion no sirve para otra");
        check(size_rejected, "una posicion fuera del tamano publicado en delta se rechaza");
        check(path_rejected, "cambiar un bit de cualquier hermano se rechaza");
        check(short_rejected, "un camino con hermanos de menos se rechaza");
        check(long_rejected, "un camino con hermanos de mas se rechaza");
        check(!tt::merkle::verify_inclusion(tt::merkle::empty_root(), 0, leaf(0), InclusionProof{}),
              "en un arbol vacio no hay nada que probar");
    }

    return tt::test::finish();
}

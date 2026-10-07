// tree.hpp
//
// Arbol de Merkle sobre los hashes de las filas de una tabla.
//
// Por que existe: el algoritmo Setup del reporte calcula un hash h_r por
// cada fila y los pone todos en el digest delta. Con 15 mil registros eso
// son cientos de KB que el cliente tendria que guardar y actualizar. El
// propio reporte (y el articulo de IntegriDB, que lo hace asi en su
// implementacion) indica que el digest se reduce a tamano constante
// poniendo esos hashes en un arbol de Merkle: delta guarda solo la raiz
// por tabla y el servidor demuestra que una fila es autentica con una
// prueba de inclusion de log2(n) hashes.
//
// Estructura: la de RFC 6962/9162 (Certificate Transparency), que esta
// definida para cualquier numero de hojas sin duplicar la ultima (duplicar
// hojas, como hace Bitcoin, permite dos listas distintas con la misma
// raiz). Se construye por niveles emparejando nodos vecinos; si un nivel
// tiene un numero impar de nodos, el ultimo sube tal cual al siguiente
// nivel. La verificacion sigue el algoritmo de RFC 9162, seccion 2.1.3.2.
//
// Las hojas son los hashes de fila (tt/db/row_hash.hpp), ordenadas por el
// valor de la columna de referencia.
//
// Etiquetas de dominio (primer byte de cada SHA-256 del esquema), para que
// ningun hash de un tipo pueda hacerse pasar por otro:
//   0x00 hoja AIT, 0x01 nodo interno AIT, 0x02 valor de nodo AIT,
//   0x03 AIT vacio                                     (tt/ait/node.cpp)
//   0x10 fila                                          (tt/db/row_hash.cpp)
//   0x11 nodo de Merkle, 0x12 arbol de Merkle vacio    (este modulo)
//   0x20 digest delta, 0x21 huella de la clave publica (tt/scheme/digest.cpp)

#pragma once

#include <cstdint>
#include <vector>

#include "tt/crypto/hash.hpp"

namespace tt::merkle {

using crypto::Digest;

// Prueba de que una hoja esta en el arbol: su posicion y los hashes
// hermanos desde la hoja hacia la raiz (de abajo hacia arriba).
struct InclusionProof {
    std::uint64_t index = 0;
    std::vector<Digest> path;
};

// Arbol de Merkle completo (lo guardan el propietario y el servidor).
class Tree {
public:
    /*
     * Crea un arbol sin hojas; su raiz es empty_root().
     */
    Tree();

    /*
     * Construye el arbol sobre las hojas dadas, en ese orden.
     *
     * Parametros:
     *   leaves  hashes de las hojas (por ejemplo, los hashes de fila)
     */
    explicit Tree(std::vector<Digest> leaves);

    /*
     * Numero de hojas.
     */
    std::uint64_t size() const { return levels_.empty() ? 0 : levels_.front().size(); }

    /*
     * Raiz del arbol: es lo que el propietario publica en delta.
     */
    const Digest& root() const { return root_; }

    /*
     * Las hojas, en orden.
     */
    const std::vector<Digest>& leaves() const;

    /*
     * Genera la prueba de inclusion de la hoja en la posicion `index`: los
     * hashes hermanos que el verificador necesita para recalcular la raiz.
     *
     * Parametros:
     *   index  posicion de la hoja (desde 0)
     *
     * Devuelve:
     *   La prueba, con a lo mas ceil(log2(n)) hashes.
     *
     * Errores:
     *   Lanza std::out_of_range si index >= size().
     */
    InclusionProof prove(std::uint64_t index) const;

private:
    std::vector<std::vector<Digest>> levels_;  // levels_[0] = hojas; el ultimo tiene la raiz
    Digest root_{};
};

/*
 * Hash de un nodo interno: H(0x11 || izquierdo || derecho).
 */
Digest node_hash(const Digest& left, const Digest& right);

/*
 * Raiz de un arbol sin hojas: H(0x12). Una tabla vacia tambien tiene raiz.
 */
Digest empty_root();

/*
 * Verifica una prueba de inclusion (lado del verificador): recalcula la
 * raiz a partir de la hoja y los hermanos, siguiendo RFC 9162 2.1.3.2, y la
 * compara con la raiz publicada.
 *
 * Parametros:
 *   root   raiz publicada en delta
 *   size   numero de hojas del arbol (tambien viene en delta)
 *   leaf   hash de la hoja que se quiere comprobar (el hash de la fila)
 *   proof  posicion y hermanos enviados por el servidor
 *
 * Devuelve:
 *   true solo si la hoja esta en esa posicion del arbol con esa raiz.
 */
bool verify_inclusion(const Digest& root, std::uint64_t size, const Digest& leaf,
                      const InclusionProof& proof);

}  // namespace tt::merkle

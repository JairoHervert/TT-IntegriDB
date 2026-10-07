// tree.hpp
//
// AIT completo del lado del servidor (ait^T_{i x j} del reporte) y su
// construccion por el Propietario de datos (AITSetup).
//
// Setup construye un arbol perfectamente balanceado a partir de las hojas
// ordenadas (el subarbol izquierdo recibe la mitad superior redondeada),
// sin rotaciones. Con las mismas filas y las mismas claves el resultado es
// siempre el mismo arbol y el mismo digest. Las actualizaciones (Insert,
// Delete con rotaciones de arbol rojinegro) se agregaran despues sobre esta
// misma estructura de nodos.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tt/ait/node.hpp"
#include "tt/ait/range_proof.hpp"
#include "tt/scheme/keys.hpp"

namespace tt::ait {

// Nodo del AIT tal como lo guarda el servidor.
struct Node {
    NodeHeader header;
    NodePayload payload;
    Digest payload_digest{};
    Digest hash{};
    std::int32_t left = -1;     // indice del hijo izquierdo en Tree::nodes()
    std::int32_t right = -1;
    std::vector<Value> values;  // solo hojas: valores de la clave, en orden ascendente
};

// AIT de un par de columnas (i, j) de una tabla.
class Tree {
public:
    /*
     * Crea un AIT vacio (sin filas); su digest es empty_tree_hash(). Los arboles
     * con datos se crean con setup().
     */
    Tree();

    /*
     * Devuelve el digest del AIT (delta_AIT en el reporte): el hash de la raiz,
     * que compromete todo el arbol. El propietario lo publica en delta y el
     * verificador lo usa para revisar las pruebas.
     */
    const Digest& root_digest() const { return root_digest_; }

    /*
     * Indica si el arbol no tiene filas.
     */
    bool empty() const { return root_ < 0; }

    /*
     * Numero de hojas, es decir, de claves distintas.
     */
    std::size_t leaf_count() const { return leaf_count_; }

    /*
     * Numero de filas (pares clave/valor) con que se construyo el arbol.
     */
    std::size_t value_count() const { return value_count_; }

    /*
     * Numero total de nodos (hojas mas internos).
     */
    std::size_t node_count() const { return nodes_.size(); }

    /*
     * Indice de la raiz dentro de nodes(), o -1 si el arbol esta vacio.
     */
    std::int32_t root() const { return root_; }

    /*
     * Acceso de solo lectura a todos los nodos. Los hijos de cada nodo se
     * encuentran por su indice (Node::left, Node::right).
     */
    const std::vector<Node>& nodes() const { return nodes_; }

    /*
     * RangeCover(lo, hi) del reporte, del lado del probador: reune los valores
     * de todas las filas con clave en [lo, hi] y construye la prueba (el arbol
     * podado) que el verificador revisara con verify_range.
     *
     * Parametros:
     *   lo, hi  limites del rango (inclusivos)
     *
     * Devuelve:
     *   Los valores en orden de clave y la prueba.
     *
     * Errores:
     *   Lanza std::invalid_argument si lo > hi.
     */
    RangeAnswer range(Key lo, Key hi) const;

    /*
     * Search(k) del reporte: los valores de una sola clave, con su prueba. Es
     * RangeCover(k, k).
     */
    RangeAnswer search(Key key) const { return range(key, key); }

private:
    friend Tree setup(std::vector<Entry> entries, const scheme::SecretKey& sk);
    friend Tree restore(std::vector<Node> nodes, std::int32_t root);

    /*
     * Agrega a la prueba, en preorden, el subarbol que empieza en `index`, y a
     * la respuesta los valores de los nodos de cobertura. Ver range().
     */
    void emit(std::int32_t index, Key lo, Key hi, RangeAnswer& answer) const;

    /*
     * Agrega a `out` los valores de todas las hojas del subarbol, de izquierda
     * a derecha (en orden de clave).
     */
    void collect_values(std::int32_t index, std::vector<Value>& out) const;

    std::vector<Node> nodes_;
    std::int32_t root_ = -1;
    Digest root_digest_{};
    std::size_t leaf_count_ = 0;
    std::size_t value_count_ = 0;
};

/*
 * AITSetup_{f_{s,sk}}(S, bottom) del reporte: construye el AIT de un par de
 * columnas. Solo el propietario puede ejecutarlo, porque necesita s para
 * los acumuladores y sk para Enc.
 *
 * Agrupa las filas por clave (una hoja por clave distinta, con el
 * multiconjunto de sus valores y un contador), arma un arbol balanceado y
 * calcula el valor y el hash de cada nodo de abajo hacia arriba.
 *
 * Parametros:
 *   entries  pares (clave, valor); el orden no importa
 *   sk       clave secreta del propietario
 *
 * Devuelve:
 *   El arbol completo, listo para entregarse al servidor.
 *
 * Errores:
 *   Lanza std::invalid_argument si algun valor es 0 (no tiene inverso).
 */
Tree setup(std::vector<Entry> entries, const scheme::SecretKey& sk);

/*
 * Reconstruye un AIT a partir de sus nodos, por ejemplo los que el servidor
 * lee de la base autenticada D~ que le entrego el propietario.
 *
 * Revisa que los nodos formen un arbol binario valido (cada nodo con un
 * solo padre, todos alcanzables desde la raiz, sin ciclos), que cada
 * encabezado sea coherente con sus hijos (minimo, maximo, clave de division
 * y contador) y que cada hoja tenga tantos valores como su contador.
 * Despues recalcula H(v_u) y el hash de cada nodo.
 *
 * Quien lo llame debe comparar root_digest() con el digest que publico el
 * propietario. Si coinciden, los encabezados, acumuladores y cifrados de
 * todos los nodos son los que produjo el propietario.
 *
 * Los valores guardados en las hojas (Node::values) no entran en ningun
 * hash: en el esquema los compromete el acumulador de la hoja, y es el
 * verificador quien comprueba con el ASO que los valores de una respuesta
 * correspondan a los acumuladores. Por eso restore no puede detectar
 * valores cambiados; el probador debe tomarlos de la base D o cotejarlos
 * con ella.
 *
 * Parametros:
 *   nodes  nodos del arbol en cualquier orden (los campos payload_digest y
 *          hash se ignoran y se recalculan)
 *   root   indice de la raiz, o -1 si el arbol esta vacio
 *
 * Devuelve:
 *   El arbol listo para responder consultas.
 *
 * Errores:
 *   Lanza std::invalid_argument con la descripcion del problema.
 */
Tree restore(std::vector<Node> nodes, std::int32_t root);

/*
 * Recupera el exponente e_u de un nodo descifrando Enc_sk(e_u). Lo usara el
 * propietario en las actualizaciones (Insert/Delete) para recalcular los
 * nodos sin recorrer todas las hojas.
 *
 * Parametros:
 *   payload  valor del nodo (recibido del servidor)
 *   sk       clave secreta del propietario
 *
 * Devuelve:
 *   e_u = prod (v^{-1} + s) del subarbol.
 *
 * Errores:
 *   Lanza crypto::DecryptionError si el cifrado fue alterado o no contiene
 *   un exponente valido.
 */
NTL::ZZ_p decrypt_exponent(const NodePayload& payload, const scheme::SecretKey& sk);

}  // namespace tt::ait

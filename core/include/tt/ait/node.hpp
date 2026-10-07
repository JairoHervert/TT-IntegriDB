// node.hpp
//
// Arbol de Intervalos Autenticado (AIT): contenido de un nodo, hashes de
// Merkle y reglas de cobertura. Lo comparten el probador (que construye las
// pruebas) y el verificador (que las revisa), para que ambos apliquen
// exactamente las mismas reglas.
//
// Correspondencia con la seccion del AIT del reporte:
//   - Hojas ordenadas por clave; una sola hoja por clave distinta (seccion
//     "Manejo de claves duplicadas"), con el multiconjunto de sus valores y
//     un contador.
//   - Clave de un nodo interno = clave maxima de su subarbol izquierdo.
//   - Valor del nodo: v_u = Enc_sk(e_u) || g^{e_u}, con
//     e_u = prod_{v en T_u} (v^{-1} + s). Como el emparejamiento es
//     asimetrico, g^{e_u} se guarda en G1 y en G2 (igual que g1_digest y
//     g2_digest en el codigo original).
//   - Hash: h_hoja = H(k || v) y h_u = H(k_u || v_u || h_izq || h_der). Aqui
//     se hashea H(v_u) en lugar de v_u (misma seguridad, pruebas mas
//     pequenas), cada tipo de hash lleva una etiqueta distinta (una hoja no
//     puede hacerse pasar por nodo interno) y cada nodo compromete tambien
//     la clave minima y maxima de su subarbol y el numero de filas. Con eso
//     el verificador puede comprobar, solo con el digest, que una prueba de
//     rango no omite ninguna hoja.

#pragma once

#include <NTL/ZZ_p.h>

#include <cstdint>
#include <vector>

#include "bn.h"
#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/hash.hpp"

namespace tt::ait {

using Key = std::int64_t;    // valor de la columna clave x_ri
using Value = std::int64_t;  // valor de la columna valor x_rj (se acumula como v^{-1})
using crypto::Digest;

// Un par (x_ri, x_rj) del conjunto S^T_{i x j} del reporte.
struct Entry {
    Key key;
    Value value;
};

// Datos del nodo que usan las reglas de cobertura.
struct NodeHeader {
    bool is_leaf = false;
    Key min_key = 0;          // clave minima del subarbol
    Key max_key = 0;          // clave maxima del subarbol
    Key split_key = 0;        // clave del nodo: hoja = su clave; interno = maxima del subarbol izquierdo
    std::uint64_t count = 0;  // numero de valores (filas) en el subarbol

    /*
     * Compara dos encabezados campo por campo.
     */
    bool operator==(const NodeHeader& o) const {
        return is_leaf == o.is_leaf && min_key == o.min_key && max_key == o.max_key &&
               split_key == o.split_key && count == o.count;
    }
};

// Valor v_u del nodo.
struct NodePayload {
    bn::Ec1 acc_g1 = crypto::zero_point<bn::Ec1>();  // g1^{e_u}
    bn::Ec2 acc_g2 = crypto::zero_point<bn::Ec2>();  // g2^{e_u}
    std::vector<unsigned char> enc;                  // Enc_sk(e_u), ver crypto/symmetric.hpp
};

// --- Hashes ---------------------------------------------------------------
/*
 * Calcula H(v_u): el hash del valor del nodo (acumuladores en G1 y G2 y el
 * cifrado del exponente). El hash del nodo incluye este digest en lugar del
 * valor completo, para que las pruebas sean mas pequenas.
 *
 * Parametros:
 *   payload  valor del nodo
 *
 * Devuelve:
 *   SHA-256 con etiqueta de dominio 0x02.
 */
Digest payload_hash(const NodePayload& payload);

/*
 * Calcula el hash de una hoja: H(0x00 || clave || contador || H(v_u)).
 * Corresponde a h_hoja = H(k || v) del reporte.
 *
 * Parametros:
 *   header          encabezado de la hoja (se usan split_key y count)
 *   payload_digest  resultado de payload_hash para la hoja
 *
 * Devuelve:
 *   El hash de la hoja.
 */
Digest leaf_hash(const NodeHeader& header, const Digest& payload_digest);

/*
 * Calcula el hash de un nodo interno:
 * H(0x01 || min || max || split || contador || H(v_u) || h_izq || h_der).
 * Corresponde a h_u = H(k_u || v_u || h_izq || h_der) del reporte, mas la
 * clave minima, la maxima y el contador, que permiten verificar que una
 * prueba de rango no omite filas.
 *
 * Parametros:
 *   header          encabezado del nodo
 *   payload_digest  resultado de payload_hash para el nodo
 *   left, right     hashes de los hijos izquierdo y derecho
 *
 * Devuelve:
 *   El hash del nodo.
 */
Digest internal_hash(const NodeHeader& header, const Digest& payload_digest, const Digest& left,
                     const Digest& right);

/*
 * Devuelve el digest de un AIT sin filas (hash de la etiqueta 0x03). Permite
 * que una tabla vacia tambien tenga un digest y que sus consultas se
 * verifiquen.
 */
Digest empty_tree_hash();

// --- Elementos del acumulador ---------------------------------------------
/*
 * Convierte un valor de la columna valor en el elemento que se acumula:
 * v^{-1} en Z_r, como indica la funcion f del reporte.
 *
 * Parametros:
 *   v  valor de la columna (distinto de cero)
 *
 * Devuelve:
 *   El inverso de v modulo r.
 *
 * Errores:
 *   Lanza std::invalid_argument si v = 0, que no tiene inverso.
 */
NTL::ZZ_p value_to_element(Value v);

// --- Reglas de cobertura para el rango [lo, hi] ----------------------------
/*
 * Indica si todo el subarbol del nodo esta dentro del rango [lo, hi], es
 * decir, si el nodo forma parte del conjunto de cobertura.
 *
 * Parametros:
 *   h       encabezado del nodo
 *   lo, hi  limites del rango (inclusivos)
 */
bool fully_inside(const NodeHeader& h, Key lo, Key hi);

/*
 * Indica si el hijo izquierdo del nodo (claves <= split) esta completamente
 * fuera del rango y si eso se puede demostrar solo con los datos del nodo:
 * cuando split < lo, o cuando min > hi. Solo en ese caso se permite podar
 * ese hijo en una prueba. La usan igual el probador y el verificador.
 */
bool left_child_outside(const NodeHeader& h, Key lo, Key hi);

/*
 * Indica si el hijo derecho del nodo (claves > split) esta completamente
 * fuera del rango y si eso se puede demostrar solo con los datos del nodo:
 * cuando split >= hi, o cuando max < lo. Solo en ese caso se permite podar
 * ese hijo en una prueba.
 */
bool right_child_outside(const NodeHeader& h, Key lo, Key hi);

}  // namespace tt::ait

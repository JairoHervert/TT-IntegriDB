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
Digest payload_hash(const NodePayload& payload);
Digest leaf_hash(const NodeHeader& header, const Digest& payload_digest);
Digest internal_hash(const NodeHeader& header, const Digest& payload_digest, const Digest& left,
                     const Digest& right);
Digest empty_tree_hash();  // digest de un AIT sin filas

// --- Elementos del acumulador ---------------------------------------------
// v^{-1} en Z_r. Lanza std::invalid_argument si v = 0 (no tiene inverso).
NTL::ZZ_p value_to_element(Value v);

// --- Reglas de cobertura para el rango [lo, hi] ----------------------------
// El nodo entero esta dentro del rango: forma parte del conjunto de
// cobertura.
bool fully_inside(const NodeHeader& h, Key lo, Key hi);
// El hijo izquierdo (claves <= split) queda completamente fuera del rango y
// eso se puede demostrar con los datos del nodo: split < lo, o min > hi.
bool left_child_outside(const NodeHeader& h, Key lo, Key hi);
// El hijo derecho (claves > split) queda completamente fuera: split >= hi,
// o max < lo.
bool right_child_outside(const NodeHeader& h, Key lo, Key hi);

}  // namespace tt::ait

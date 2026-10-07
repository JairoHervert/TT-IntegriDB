// row_hash.hpp
//
// Compromiso de una fila completa: h_r = H(x_r1 || ... || x_rm) del
// algoritmo Setup del reporte. Protege todas las columnas de la fila,
// incluidas las de texto, que no participan en ningun AIT.
//
// Codificacion canonica (SHA-256):
//   0x10 || nombre de la tabla (con longitud) || numero de columnas (u64)
//   y por cada celda: 0x00 || entero (i64)   o   0x01 || texto (con longitud)
// La marca de tipo impide que el entero 1 y el texto "1" den el mismo hash,
// y las longitudes impiden que ("ab", "c") y ("a", "bc") coincidan. El
// nombre de la tabla evita que una fila de una tabla pase por fila de otra
// con la misma forma.

#pragma once

#include "tt/crypto/hash.hpp"
#include "tt/db/table.hpp"

namespace tt::db {

/*
 * Calcula el hash de una fila con la codificacion canonica descrita arriba.
 * Es la hoja que representa a la fila en el arbol de Merkle de su tabla.
 *
 * Parametros:
 *   schema  estructura de la tabla a la que pertenece la fila
 *   row     la fila
 *
 * Devuelve:
 *   El hash de 32 bytes.
 *
 * Errores:
 *   Lanza DataError si la fila no tiene una celda por columna o si el tipo
 *   de alguna celda no coincide con el de su columna.
 */
crypto::Digest row_hash(const TableSchema& schema, const Row& row);

}  // namespace tt::db

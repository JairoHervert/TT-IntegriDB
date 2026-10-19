// response_io.hpp
//
// Formato binario de la respuesta (R, pi) que el probador envia al
// verificador. Hoy viaja como archivo; cuando se conecte la red, estos
// mismos bytes seran el mensaje.
//
//   "TTRSP\0\0\0" | formato (u32) | tipo de respuesta (u8, 1 = seleccion/rango)
//   id del estado (32 bytes)
//   consulta: tabla, columna, lo (i64), hi (i64)
//   prueba del AIT: numero de entradas (u32) y cada entrada en preorden:
//     tipo (u8: 0 cobertura, 1 expandido, 2 podado)
//     cobertura: encabezado, g1^{e_u}, g2^{e_u}, Enc(e_u) y, si es nodo
//                interno, los hashes de sus dos hijos
//     expandido: encabezado y H(v_u)
//     podado:    hash del subarbol
//     encabezado = es_hoja (u8), min, max, clave (i64), contador (u64)
//   filas: numero (u32) y por cada una:
//     celdas (u32) y cada celda: tipo (u8: 0 entero, 1 texto) y valor
//     prueba de Merkle: posicion (u64), hermanos (u32) y cada hermano (32 bytes)
//
// El lector no confia en los bytes: revisa cada longitud antes de reservar
// memoria, limita tamanos y rechaza basura al final. Que la respuesta tenga
// buen formato no la hace valida; eso lo decide verify_selection.
//
// Los puntos de G2 de los nodos de cobertura no se revisan contra el
// subgrupo de orden r al leerlos: entran al hash del nodo, que debe llevar a
// la raiz publicada en delta, asi que solo se aceptan los que construyo el
// propietario (ver io/binary.hpp).

#pragma once

#include <filesystem>
#include <stdexcept>
#include <vector>

#include "tt/query/selection.hpp"
#include "tt/query/multirange.hpp"

namespace tt::query {

// La respuesta no tiene el formato esperado (truncada, alterada o de otro
// tipo).
class ResponseFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Convierte una respuesta en los bytes del formato descrito arriba.
 */
std::vector<unsigned char> encode_response(const SelectionResponse& response);

/*
 * Reconstruye una respuesta a partir de sus bytes.
 *
 * Errores:
 *   Lanza ResponseFormatError con el problema encontrado.
 */
SelectionResponse decode_response(const std::vector<unsigned char>& data);

/*
 * Guarda una respuesta en un archivo (escritura atomica).
 *
 * Devuelve:
 *   El tamano en bytes de lo escrito.
 */
std::size_t save_response(const std::filesystem::path& path, const SelectionResponse& response);

/*
 * Carga una respuesta de un archivo con decode_response.
 *
 * Errores:
 *   Lanza std::runtime_error si no se puede leer y ResponseFormatError (con
 *   la ruta) si el contenido no es valido.
 */
SelectionResponse load_response(const std::filesystem::path& path);


// Tipo 2 del formato TTRSP: preserva sin cambios el formato tipo 1 de C1-C6.
std::vector<unsigned char> encode_multirange_response(const MultiRangeResponse& response);
MultiRangeResponse decode_multirange_response(const std::vector<unsigned char>& data);
std::size_t save_multirange_response(const std::filesystem::path& path, const MultiRangeResponse& response);
MultiRangeResponse load_multirange_response(const std::filesystem::path& path);

}  // namespace tt::query

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
#include "tt/query/count_intersection.hpp"

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

/*
 * Serializa la respuesta C7: digest, dos rangos, pruebas AIT, testigos
 * ASO de separacion y coprimalidad, y filas con sus pruebas Merkle.
 *
 * Parametros:
 *   response  respuesta multidimensional generada por el probador
 *
 * Devuelve:
 *   Los bytes del formato TTRSP de tipo 2.
 */
std::vector<unsigned char> encode_multirange_response(const MultiRangeResponse& response);

/*
 * Reconstruye una respuesta C7 desde bytes no confiables. Comprueba la
 * cabecera TTRSP, el tipo 2, las longitudes y el fin exacto del mensaje.
 * La lectura del formato no implica aceptar sus pruebas criptograficas.
 *
 * Parametros:
 *   data  bytes de una respuesta C7
 *
 * Devuelve:
 *   La respuesta decodificada, pendiente de verify_multirange.
 *
 * Errores:
 *   Lanza ResponseFormatError si la cabecera, el tipo, los tamanos, los
 *   puntos codificados o el contenido de la respuesta son invalidos.
 */
MultiRangeResponse decode_multirange_response(const std::vector<unsigned char>& data);

/*
 * Codifica una respuesta C7 y la guarda mediante escritura atomica.
 *
 * Parametros:
 *   path      ruta del archivo de salida
 *   response  respuesta C7 que se desea guardar
 *
 * Devuelve:
 *   Tamano de la respuesta guardada, en bytes.
 *
 * Errores:
 *   Propaga los errores de escritura del archivo.
 */
std::size_t save_multirange_response(const std::filesystem::path& path, const MultiRangeResponse& response);

/*
 * Lee y decodifica una respuesta C7 desde un archivo.
 *
 * Parametros:
 *   path  ruta del archivo de respuesta
 *
 * Devuelve:
 *   La respuesta C7 decodificada, aun pendiente de validacion.
 *
 * Errores:
 *   Propaga errores de lectura y lanza ResponseFormatError con la ruta
 *   del archivo cuando los bytes no representan una respuesta C7 valida.
 */
MultiRangeResponse load_multirange_response(const std::filesystem::path& path);

/*
 * Tipo 3 de TTRSP: serializa COUNT y sus testigos de interseccion ASO.
 *
 * Parametros:
 *   response / data  respuesta C12 o sus bytes
 *   path             archivo que se lee o escribe
 *
 * Devuelve:
 *   Bytes, respuesta reconstruida o numero de bytes guardados.
 *
 * Errores:
 *   Las funciones de lectura lanzan ResponseFormatError si el mensaje
 *   contiene una cabecera, tipo, tamanos o puntos invalidos.
 */
std::vector<unsigned char> encode_count_response(const CountResponse& response);
CountResponse decode_count_response(const std::vector<unsigned char>& data);
std::size_t save_count_response(const std::filesystem::path& path, const CountResponse& response);
CountResponse load_count_response(const std::filesystem::path& path);

}  // namespace tt::query

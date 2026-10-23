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
#include "tt/query/average.hpp"
#include "tt/query/join_program.hpp"
#include "tt/query/join_course.hpp"

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
 * Serializa COUNT (C11/C12) con sus dos RangeCover y testigos ASO.
 *
 * Parametros:
 *   response  resultado COUNT y prueba generada por el probador
 *
 * Devuelve:
 *   Bytes del formato TTRSP tipo 3; aun no estan verificados.
 */
std::vector<unsigned char> encode_count_response(const CountResponse& response);

/*
 * Reconstruye una respuesta C11/C12 desde bytes no confiables.
 *
 * Parametros:
 *   data  secuencia binaria de respuesta tipo 3
 *
 * Devuelve:
 *   Respuesta COUNT decodificada, pendiente de verify_count.
 *
 * Errores:
 *   Lanza ResponseFormatError ante cabecera, tipo o campos invalidos.
 */
CountResponse decode_count_response(const std::vector<unsigned char>& data);

/*
 * Guarda una respuesta C11/C12 en un archivo de forma atomica.
 *
 * Parametros:
 *   path      archivo destino
 *   response  respuesta COUNT que se desea guardar
 *
 * Devuelve:
 *   Numero de bytes escritos.
 *
 * Errores:
 *   Propaga los errores de escritura.
 */
std::size_t save_count_response(const std::filesystem::path& path, const CountResponse& response);

/*
 * Carga una respuesta C11/C12 desde un archivo.
 *
 * Parametros:
 *   path  archivo fuente
 *
 * Devuelve:
 *   Respuesta COUNT decodificada, todavia no verificada.
 *
 * Errores:
 *   Propaga errores de lectura y ResponseFormatError del decodificador.
 */
CountResponse load_count_response(const std::filesystem::path& path);

// TTRSP tipo 4: C10 SUM+COUNT, RangeCover y testigos bilineales compactos.

/*
 * Serializa la respuesta C10 al formato TTRSP tipo 4.
 *
 * Parametros:
 *   response  respuesta C10 creada por el probador
 *
 * Devuelve:
 *   Bytes de la respuesta; no constituyen una validacion criptografica.
 */
std::vector<unsigned char> encode_average_response(const AverageResponse& response);

/*
 * Reconstruye C10 desde bytes no confiables y comprueba su estructura.
 *
 * Parametros:
 *   data  secuencia binaria recibida del probador
 *
 * Devuelve:
 *   Respuesta decodificada, pendiente de verify_average.
 *
 * Errores:
 *   Lanza ResponseFormatError si el formato es invalido.
 */
AverageResponse decode_average_response(const std::vector<unsigned char>& data);

/*
 * Guarda una respuesta C10 en disco mediante escritura atomica.
 *
 * Parametros:
 *   path      ruta del archivo destino
 *   response  respuesta que se guardara
 *
 * Devuelve:
 *   Numero de bytes escritos.
 *
 * Errores:
 *   Propaga los errores de escritura del archivo.
 */
std::size_t save_average_response(const std::filesystem::path& path, const AverageResponse& response);

/*
 * Lee de disco una respuesta TTRSP tipo 4.
 *
 * Parametros:
 *   path  ruta del archivo que contiene la respuesta
 *
 * Devuelve:
 *   Respuesta C10 decodificada, aun sin verificar.
 *
 * Errores:
 *   Propaga errores de lectura y ResponseFormatError si el contenido
 *   no representa una respuesta C10 valida.
 */
AverageResponse load_average_response(const std::filesystem::path& path);

/*
 * Codifica la respuesta C9 en el formato TTRSP tipo 5, separado de C8.
 *
 * Parametros:
 *   response  respuesta C9 con AIT, ASO y pruebas Merkle
 *
 * Devuelve:
 *   Bytes transferibles al Verifier, todavia no validados.
 */
std::vector<unsigned char> encode_join_program_response(const JoinProgramResponse& response);

/*
 * Decodifica el mensaje C9 de tipo 5 desde bytes no confiables.
 *
 * Parametros:
 *   data  bytes recibidos del Prover
 *
 * Devuelve:
 *   Respuesta pendiente de verify_join_program.
 *
 * Errores:
 *   Lanza ResponseFormatError ante tipos o longitudes invalidas,
 *   mensajes truncados y bytes sobrantes.
 */
JoinProgramResponse decode_join_program_response(const std::vector<unsigned char>& data);

/*
 * Guarda el mensaje C9 mediante escritura atomica.
 *
 * Parametros:
 *   path      ruta de salida
 *   response  respuesta C9 generada por el Prover
 *
 * Devuelve:
 *   Cantidad de bytes escritos.
 *
 * Errores:
 *   Propaga los errores de escritura.
 */
std::size_t save_join_program_response(const std::filesystem::path& path, const JoinProgramResponse& response);

/*
 * Lee y decodifica un mensaje C9 desde un archivo.
 *
 * Parametros:
 *   path  archivo de respuesta TTRSP tipo 5
 *
 * Devuelve:
 *   Respuesta C9 sin verificar.
 *
 * Errores:
 *   Propaga los errores de lectura y ResponseFormatError.
 */
JoinProgramResponse load_join_program_response(const std::filesystem::path& path);

/*
 * Serializa C8 en TTRSP tipo 6: dos AIT, testigos ASO y filas Merkle.
 *
 * Parametros:
 *   response  respuesta construida por el Prover
 *
 * Devuelve:
 *   Bytes para transferir al Verifier sin compartir D~ ni la clave secreta.
 */
std::vector<unsigned char> encode_join_course_response(const JoinCourseResponse& response);

/*
 * Decodifica bytes potencialmente manipulados del mensaje C8.
 *
 * Parametros:
 *   data  contenido binario recibido del Prover
 *
 * Devuelve:
 *   Respuesta no confiable; requiere verify_join_course.
 *
 * Errores:
 *   Lanza ResponseFormatError si el formato esta alterado o incompleto.
 */
JoinCourseResponse decode_join_course_response(const std::vector<unsigned char>& data);

/*
 * Escribe la respuesta C8 de forma atomica.
 *
 * Parametros:
 *   path      ruta del archivo de intercambio
 *   response  respuesta binaria autenticable
 *
 * Devuelve:
 *   Numero de bytes escritos.
 */
std::size_t save_join_course_response(const std::filesystem::path& path, const JoinCourseResponse& response);

/*
 * Lee una respuesta C8 desde el medio de intercambio temporal.
 *
 * Parametros:
 *   path  ruta del archivo
 *
 * Devuelve:
 *   Respuesta pendiente de validacion criptografica.
 *
 * Errores:
 *   Propaga errores de lectura o ResponseFormatError si es invalida.
 */
JoinCourseResponse load_join_course_response(const std::filesystem::path& path);

}  // namespace tt::query

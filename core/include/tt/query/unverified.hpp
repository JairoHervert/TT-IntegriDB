// unverified.hpp
//
// Consultas no verificables (N1 a N5). Son consultas utiles para el caso de
// estudio que el esquema no puede respaldar con una prueba: el probador las
// ejecuta en MySQL y entrega el resultado SIN (R, pi), y el verificador lo
// presenta marcado como no verificado. Nunca pasan por Verify ni se aceptan
// en /v1/query; sirven para mostrar el limite del esquema frente a lo que si
// verifica (C1 a C14).
//
// Decisiones:
//   - Catalogo fijo. La consulta se lee con reglas estrictas y el SQL se
//     arma con plantillas; no se acepta SQL escrito por el usuario.
//   - El resultado viaja en un formato binario propio (TTUNV) que el
//     verificador decodifica y compara con las columnas esperadas antes de
//     escribir su propio JSON. Asi el servidor no puede agregar campos al
//     JSON que ve el navegador (por ejemplo, un veredicto "verified").
//   - TTUNV no lleva ningun campo de integridad: decodificarlo bien solo
//     significa que el formato es correcto, no que los datos lo sean.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tt/query/verifier_service.hpp"  // ResultValue

namespace tt::query {

// Consultas del catalogo no verificable.
enum class UnverifiedKind {
    kSurnamePrefix,   // N1: alumnos cuyo apellido paterno empieza con un texto (LIKE)
    kAverageByTerm,   // N2: promedio de un alumno por periodo (GROUP BY)
    kMaximum,         // N3: calificacion maxima de un alumno (MAX)
    kFullTranscript,  // N4: kardex con programa y unidad (JOIN de cuatro tablas)
    kTopAverages,     // N5: alumnos con mejor promedio (GROUP BY, ORDER BY y LIMIT)
};

// Una consulta no verificable ya leida y validada.
struct UnverifiedQuery {
    UnverifiedKind kind = UnverifiedKind::kMaximum;
    std::string catalog_id;  // N1 ... N5
    std::string text_value;  // N1: prefijo del apellido (solo letras ASCII)
    std::int64_t number = 0; // N2 a N4: student_id; N5: cuantos alumnos
};

// Tipo de una columna del resultado (codigo que viaja en TTUNV).
enum class UnverifiedType : std::uint8_t {
    kInteger = 1,
    kReal = 2,
    kText = 3,
};

// Columna del resultado: nombre que se muestra y tipo.
struct UnverifiedColumn {
    std::string name;
    UnverifiedType type = UnverifiedType::kInteger;

    bool operator==(const UnverifiedColumn& o) const { return name == o.name && type == o.type; }
};

// Resultado que entrega el probador. row_count es el numero de filas que
// devolvio MySQL; rows puede tener menos si se recorto por tamano.
struct UnverifiedResult {
    std::string catalog_id;
    std::vector<UnverifiedColumn> columns;
    std::vector<std::vector<ResultValue>> rows;
    std::uint64_t row_count = 0;
};

// Texto de ayuda con las formas N1 a N5.
extern const char* const kUnverifiedForms;

/*
 * Indica si las palabras empiezan con un id del catalogo no verificable
 * (N1 a N5). No revisa los parametros.
 *
 * Parametros:
 *   words  palabras de la consulta
 *
 * Devuelve:
 *   true si la primera palabra es N1, N2, N3, N4 o N5.
 */
bool is_unverified_query(const std::vector<std::string>& words);

/*
 * Lee una consulta no verificable:
 *
 *   N1 <prefijo>      alumnos cuyo apellido paterno empieza con <prefijo>
 *                     (1 a 30 letras ASCII, sin comodines)
 *   N2 <student_id>   promedio y numero de evaluaciones por periodo
 *   N3 <student_id>   calificacion maxima
 *   N4 <student_id>   kardex completo con programa y unidad de aprendizaje
 *   N5 <n>            los <n> alumnos con mejor promedio (1 a 100)
 *
 * Parametros:
 *   words  las palabras de la consulta
 *
 * Devuelve:
 *   La consulta validada.
 *
 * Errores:
 *   Lanza QueryError si el id no existe, si falta o sobra un valor o si un
 *   valor no cumple su regla.
 */
UnverifiedQuery parse_unverified_query(const std::vector<std::string>& words);

/*
 * Escribe el SQL de la consulta a partir de su plantilla. Los valores ya
 * fueron validados (enteros o letras), por lo que no pueden cambiar la
 * forma de la sentencia.
 *
 * Parametros:
 *   query  consulta validada
 *
 * Devuelve:
 *   La sentencia SELECT.
 */
std::string unverified_sql(const UnverifiedQuery& query);

/*
 * Columnas que debe tener el resultado, en orden. El verificador las usa
 * para rechazar un resultado con otra forma.
 *
 * Parametros:
 *   query  consulta validada
 *
 * Devuelve:
 *   Nombres y tipos de las columnas.
 */
std::vector<UnverifiedColumn> unverified_columns(const UnverifiedQuery& query);

/*
 * Motivo por el que el esquema no puede verificar la consulta, para
 * mostrarlo al usuario.
 *
 * Parametros:
 *   query  consulta validada
 *
 * Devuelve:
 *   Una frase en espanol.
 */
std::string unverified_reason(const UnverifiedQuery& query);

/*
 * Codifica un resultado en formato TTUNV:
 *   "TTUNV" | version u8 | id texto | columnas u32 | (nombre texto, tipo u8)*
 *   | row_count u64 | filas u32 | por celda: etiqueta u8 (0 NULL, 1 entero,
 *   2 real, 3 texto) y su valor (i64, double IEEE como u64 o texto).
 *
 * Parametros:
 *   result  resultado a codificar
 *
 * Devuelve:
 *   Los bytes.
 */
std::vector<unsigned char> encode_unverified_result(const UnverifiedResult& result);

/*
 * Lee un resultado TTUNV recibido del probador y revisa que corresponda a
 * la consulta pedida: mismo id, mismas columnas, celdas del tipo de su
 * columna (o NULL), reales finitos, row_count >= filas y nada sobrante.
 * Que pase la revision NO dice nada de la integridad de los datos.
 *
 * Parametros:
 *   bytes     cuerpo recibido (no confiable)
 *   expected  consulta que se envio
 *
 * Devuelve:
 *   El resultado leido.
 *
 * Errores:
 *   Lanza io::FormatError si el formato no es el esperado.
 */
UnverifiedResult decode_unverified_result(const std::vector<unsigned char>& bytes, const UnverifiedQuery& expected);

}  // namespace tt::query

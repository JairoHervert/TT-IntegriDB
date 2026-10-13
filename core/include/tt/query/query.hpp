// query.hpp
//
// Consultas de seleccion y de rango sobre una columna (C1 a C6 del catalogo
// del caso de estudio), y la forma en que las escriben el probador y el
// verificador en la linea de comandos.
//
//     SELECT * FROM T WHERE c = v              (seleccion: lo = hi = v)
//     SELECT * FROM T WHERE c BETWEEN lo AND hi  (rango)
//
// Como se verifican (seccion 4.3 del articulo de IntegriDB): RangeCover en
// el AIT (c -> columna de referencia de T) da los identificadores de todas
// las filas con c en [lo, hi], sin omitir ninguno; cada fila se comprueba
// con su prueba de Merkle contra la raiz de T en delta. Por eso una consulta
// solo se puede verificar si ese AIT existe en delta; si no, es una consulta
// no soportada (RNF05) y nunca se acepta una respuesta sin prueba.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt/scheme/digest.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace tt::query {

// SELECT * FROM table WHERE column BETWEEN lo AND hi (o = lo si lo == hi).
struct RangeQuery {
    std::string table;
    std::string column;
    std::int64_t lo = 0;
    std::int64_t hi = 0;

    /*
     * Compara los cuatro campos.
     */
    bool operator==(const RangeQuery& o) const {
        return table == o.table && column == o.column && lo == o.lo && hi == o.hi;
    }

    /*
     * Indica si es una seleccion por igualdad (lo == hi).
     */
    bool is_equality() const { return lo == hi; }
};

// Una consulta leida de la linea de comandos.
struct ParsedQuery {
    std::string catalog_id;  // C1 ... C6, o vacio si se escribio con "range"
    RangeQuery query;
};

// La consulta esta mal escrita (forma desconocida, numero invalido, lo > hi...).
class QueryError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// La consulta esta bien escrita pero el estado autenticado (delta) no tiene
// el AIT que su verificacion necesita (RNF05).
class UnsupportedQuery : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Texto de ayuda con las formas aceptadas por parse_query.
extern const char* const kQueryForms;

/*
 * Lee una consulta escrita como palabras de la linea de comandos:
 *
 *   C1 <enrollment_number>     student, seleccion por boleta
 *   C2 <student_id>            academic_record, registros de un alumno
 *   C3 <course_id>             course, una unidad de aprendizaje
 *   C4 <min> <max>             academic_record, grade en un intervalo
 *   C5 <periodo1> <periodo2>   academic_record, term_code en un intervalo
 *   C6 <periodo1> <periodo2>   student, admission_term en un intervalo
 *   range <tabla> <columna> <min> <max>   cualquier rango en una columna
 *
 * Parametros:
 *   words  las palabras de la consulta (sin opciones)
 *
 * Devuelve:
 *   La consulta y, si se uso un id del catalogo, ese id.
 *
 * Errores:
 *   Lanza QueryError si la forma no existe, si falta o sobra un valor, si
 *   un numero no es un entero de 64 bits, si un nombre no es un
 *   identificador valido o si min > max.
 */
ParsedQuery parse_query(const std::vector<std::string>& words);

/*
 * Escribe la consulta como SQL, para mostrarla al usuario.
 *
 * Devuelve:
 *   Por ejemplo "SELECT * FROM academic_record WHERE grade BETWEEN 6 AND 8".
 */
std::string to_sql(const RangeQuery& query);

/*
 * Determina el AIT con el que se verifica la consulta: (columna filtrada ->
 * columna de referencia de la tabla), y comprueba que exista en delta.
 *
 * Parametros:
 *   query   la consulta
 *   digest  el digest delta vigente
 *
 * Devuelve:
 *   La identificacion del arbol.
 *
 * Errores:
 *   Lanza UnsupportedQuery si la tabla o la columna no existen, si la
 *   columna es de texto o si ese AIT no se construyo en Setup.
 */
scheme::TreeSpec selection_tree(const RangeQuery& query, const scheme::DatabaseDigest& digest);

}  // namespace tt::query

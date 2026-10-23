// query.cpp

#include "tt/query/query.hpp"

#include <limits>

#include "tt/db/table.hpp"

namespace tt::query {

const char* const kQueryForms =
    "      C1 <boleta>                   SELECT * FROM student WHERE enrollment_number = <boleta>\n"
    "      C2 <student_id>               SELECT * FROM academic_record WHERE student_id = <student_id>\n"
    "      C3 <course_id>                SELECT * FROM course WHERE course_id = <course_id>\n"
    "      C4 <min> <max>                SELECT * FROM academic_record WHERE grade BETWEEN <min> AND <max>\n"
    "      C5 <periodo1> <periodo2>      SELECT * FROM academic_record WHERE term_code BETWEEN ...\n"
    "      C6 <periodo1> <periodo2>      SELECT * FROM student WHERE admission_term BETWEEN ...\n"
    "      C7 <grade_min> <grade_max> <term_min> <term_max>   rango multidimensional (ASO)\n"
    "      C8                            JOIN de academic_record y course (ASO + Merkle)\n"
    "      C9                            JOIN de student y program (ASO + Merkle)\n"
    "      C10 <student_id>               AVG verificado con SUM ASO y COUNT AIT\n"
    "      C11 <student_id>               COUNT de calificaciones menores que 6 (ASO)\n"
    "      C12 <student_id> <evaluation_type>  COUNT verificable ASO (sin leer filas MySQL)\n"
    "      C13 <student_id>               MIN verificable como rango grade <= minimo (ASO)\n"
    "      range <tabla> <columna> <min> <max>   rango en cualquier columna entera\n"
    "      (los periodos se escriben como en la base, por ejemplo 20241 = 2024-1)\n";

namespace {

// Una consulta del catalogo que es seleccion o rango sobre una columna.
struct CatalogForm {
    const char* id;
    const char* table;
    const char* column;
    bool range;  // true: dos valores (min max); false: uno (igualdad)
};

// Las consultas C1 a C6 del catalogo (core/src/scheme/tree_plan.cpp). La
// prueba query_test comprueba que cada una use el mismo AIT que el
// catalogo le asigna.
const CatalogForm kCatalogForms[] = {
    {"C1", "student", "enrollment_number", false},
    {"C2", "academic_record", "student_id", false},
    {"C3", "course", "course_id", false},
    {"C4", "academic_record", "grade", true},
    {"C5", "academic_record", "term_code", true},
    {"C6", "student", "admission_term", true},
};

/*
 * Convierte un texto en entero de 64 bits. Acepta un signo menos y exige
 * forma canonica (sin ceros a la izquierda ni signo +), para que cada
 * numero tenga una sola escritura.
 *
 * Errores:
 *   Lanza QueryError si el texto no es un entero valido.
 */
std::int64_t parse_integer(const std::string& text) {
    const bool negative = !text.empty() && text[0] == '-';
    const std::string digits = negative ? text.substr(1) : text;
    bool ok = !digits.empty() && digits.size() <= 19 && (digits.size() == 1 || digits[0] != '0') &&
              !(negative && digits == "0");
    std::uint64_t v = 0;
    for (char ch : digits) {
        if (ch < '0' || ch > '9') ok = false;
        if (ok) v = v * 10 + static_cast<std::uint64_t>(ch - '0');  // 19 digitos no desbordan u64
    }
    const std::uint64_t max_positive = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (ok && (negative ? v > max_positive + 1 : v > max_positive)) ok = false;
    if (!ok) throw QueryError("'" + text + "' no es un entero valido");
    if (!negative) return static_cast<std::int64_t>(v);
    return v == max_positive + 1 ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(v);
}

/*
 * Exige que la consulta tenga exactamente `expected` palabras.
 *
 * Errores:
 *   Lanza QueryError indicando la forma correcta.
 */
void expect_words(const std::vector<std::string>& words, std::size_t expected, const std::string& form) {
    if (words.size() != expected) throw QueryError("la consulta " + words[0] + " se escribe: " + form);
}

/*
 * Revisa los limites del rango.
 *
 * Errores:
 *   Lanza QueryError si lo > hi.
 */
void check_bounds(const RangeQuery& q) {
    if (q.lo > q.hi) {
        throw QueryError("el rango esta invertido: " + std::to_string(q.lo) + " > " + std::to_string(q.hi));
    }
}

}  // namespace

/*
 * Implementacion: busca la primera palabra entre las formas del catalogo y
 * "range"; despues lee los valores que esa forma necesita.
 */
ParsedQuery parse_query(const std::vector<std::string>& words) {
    if (words.empty()) throw QueryError("falta la consulta");
    ParsedQuery out;
    if (words[0] == "range") {
        expect_words(words, 5, "range <tabla> <columna> <min> <max>");
        if (!db::is_valid_identifier(words[1]) || !db::is_valid_identifier(words[2])) {
            throw QueryError("nombre de tabla o columna invalido");
        }
        out.query = RangeQuery{words[1], words[2], parse_integer(words[3]), parse_integer(words[4])};
        check_bounds(out.query);
        return out;
    }
    if (words[0] == "C8") {
        expect_words(words, 1, "C8");
        out.catalog_id = "C8";
        out.join_courses = true;
        return out;
    }
    if (words[0] == "C9") {
        expect_words(words, 1, "C9");
        out.catalog_id = "C9";
        out.join_programs = true;
        return out;
    }
    if (words[0] == "C10") {
        expect_words(words, 2, "C10 <student_id>");
        const auto student = parse_integer(words[1]);
        out.catalog_id = "C10";
        out.query = RangeQuery{"academic_record", "student_id", student, student};
        out.avg_student = student;
        return out;
    }
    if (words[0] == "C11") {
        expect_words(words, 2, "C11 <student_id>");
        const auto student = parse_integer(words[1]);
        out.catalog_id = "C11";
        out.query = RangeQuery{"academic_record", "student_id", student, student};
        // grade < 6 equivale exactamente a [INT64_MIN, 5] para claves enteras.
        out.count = CountQuery{"C11", out.query,
                               RangeQuery{"academic_record", "grade",
                                          std::numeric_limits<std::int64_t>::min(), 5}};
        return out;
    }
    if (words[0] == "C12") {
        expect_words(words, 3, "C12 <student_id> <evaluation_type>");
        const auto student = parse_integer(words[1]);
        const auto evaluation = parse_integer(words[2]);
        out.catalog_id = "C12";
        out.query = RangeQuery{"academic_record", "student_id", student, student};
        out.count = CountQuery{"C12", out.query,
                               RangeQuery{"academic_record", "evaluation_type", evaluation, evaluation}};
        return out;
    }
    if (words[0] == "C13") {
        expect_words(words, 2, "C13 <student_id>");
        const auto student = parse_integer(words[1]);
        out.catalog_id = "C13";
        out.query = RangeQuery{"academic_record", "student_id", student, student};
        out.min_student = student;
        return out;
    }
    if (words[0] == "C7") {
        expect_words(words, 5, "C7 <grade_min> <grade_max> <term_min> <term_max>");
        out.catalog_id = "C7";
        out.query = RangeQuery{"academic_record", "grade", parse_integer(words[1]), parse_integer(words[2])};
        RangeQuery second{"academic_record", "term_code", parse_integer(words[3]), parse_integer(words[4])};
        check_bounds(out.query);
        check_bounds(second);
        out.multi = MultiRangeQuery{out.query, second};
        return out;
    }
    for (const CatalogForm& f : kCatalogForms) {
        if (words[0] != f.id) continue;
        out.catalog_id = f.id;
        out.query.table = f.table;
        out.query.column = f.column;
        if (f.range) {
            expect_words(words, 3, std::string(f.id) + " <min> <max>");
            out.query.lo = parse_integer(words[1]);
            out.query.hi = parse_integer(words[2]);
        } else {
            expect_words(words, 2, std::string(f.id) + " <valor>");
            out.query.lo = out.query.hi = parse_integer(words[1]);
        }
        check_bounds(out.query);
        return out;
    }
    throw QueryError("consulta desconocida '" + words[0] +
                     "'; por ahora se verifican C1 a C13 y 'range'");
}

/*
 * Implementacion: igualdad si lo == hi; BETWEEN en otro caso.
 */
std::string to_sql(const RangeQuery& q) {
    std::string sql = "SELECT * FROM " + q.table + " WHERE " + q.column;
    if (q.is_equality()) return sql + " = " + std::to_string(q.lo);
    return sql + " BETWEEN " + std::to_string(q.lo) + " AND " + std::to_string(q.hi);
}

/*
 * Implementacion: concatena los dos predicados BETWEEN de C7.
 * No busca un arbol en delta ni ejecuta la consulta en MySQL.
 */
std::string to_sql(const MultiRangeQuery& q) {
    return "SELECT * FROM " + q.first.table + " WHERE " + q.first.column + " BETWEEN " +
           std::to_string(q.first.lo) + " AND " + std::to_string(q.first.hi) + " AND " +
           q.second.column + " BETWEEN " + std::to_string(q.second.lo) + " AND " +
           std::to_string(q.second.hi);
}

/*
 * Escribe el COUNT del catalogo sin modificar los predicados del usuario.
 */
std::string to_sql(const CountQuery& q) {
    const std::string predicate = q.catalog_id == "C11"
        ? q.second.column + " < 6"
        : q.second.column + " = " + std::to_string(q.second.lo);
    return "SELECT COUNT(*) FROM " + q.first.table + " WHERE " + q.first.column + " = " +
           std::to_string(q.first.lo) + " AND " + predicate;
}

scheme::TreeSpec selection_tree(const RangeQuery& q, const scheme::DatabaseDigest& digest) {
    const scheme::TableDigest* table = digest.find_table(q.table);
    if (table == nullptr) throw UnsupportedQuery("la tabla " + q.table + " no existe en el estado autenticado");
    const std::optional<std::size_t> column = table->schema.column_index(q.column);
    if (!column) throw UnsupportedQuery("la tabla " + q.table + " no tiene la columna " + q.column);
    if (table->schema.columns[*column].type != db::ColumnType::kInteger) {
        throw UnsupportedQuery("la columna " + q.column + " es de texto; solo se verifican rangos sobre columnas enteras");
    }
    const scheme::TreeSpec spec{q.table, q.column, table->schema.columns[table->schema.reference_column].name};
    if (digest.find_tree(spec) == nullptr) {
        throw UnsupportedQuery("el estado autenticado no tiene el AIT " + spec.label() +
                               " (plan '" + digest.plan + "'); la consulta no se puede verificar");
    }
    return spec;
}

}  // namespace tt::query

// unverified.cpp

#include "tt/query/unverified.hpp"

#include <cmath>
#include <cstring>
#include <variant>

#include "tt/io/binary.hpp"
#include "tt/query/query.hpp"

namespace tt::query {

const char* const kUnverifiedForms =
    "      N1 <prefijo>       alumnos cuyo apellido paterno empieza con <prefijo> (LIKE)\n"
    "      N2 <student_id>    promedio por periodo de un alumno (GROUP BY)\n"
    "      N3 <student_id>    calificacion maxima de un alumno (MAX)\n"
    "      N4 <student_id>    kardex con programa y unidad (JOIN de cuatro tablas)\n"
    "      N5 <n>             los <n> alumnos con mejor promedio, 1 a 100 (ORDER BY + LIMIT)\n"
    "      (N1 a N5 se entregan SIN verificar: el esquema no tiene prueba para ellas)\n";

namespace {

// Marca y version del formato TTUNV.
const char kMagic[] = {'T', 'T', 'U', 'N', 'V'};
constexpr std::uint8_t kVersion = 1;

// Etiquetas de las celdas en TTUNV.
constexpr std::uint8_t kTagNull = 0;

// Limites de lectura: ninguna consulta del catalogo los alcanza.
constexpr std::size_t kMaxIdBytes = 8;
constexpr std::size_t kMaxNameBytes = 64;
constexpr std::size_t kMaxTextBytes = 4096;
constexpr std::size_t kMaxColumns = 16;

// Longitud maxima del prefijo de N1 y del numero de alumnos de N5.
constexpr std::size_t kMaxPrefix = 30;
constexpr std::int64_t kMaxTop = 100;

// Una forma del catalogo: id, tipo de consulta y texto del parametro.
struct Form {
    const char* id;
    UnverifiedKind kind;
    const char* usage;
};

const Form kForms[] = {
    {"N1", UnverifiedKind::kSurnamePrefix, "N1 <prefijo del apellido paterno>"},
    {"N2", UnverifiedKind::kAverageByTerm, "N2 <student_id>"},
    {"N3", UnverifiedKind::kMaximum, "N3 <student_id>"},
    {"N4", UnverifiedKind::kFullTranscript, "N4 <student_id>"},
    {"N5", UnverifiedKind::kTopAverages, "N5 <n>"},
};

/*
 * Busca la forma de un id; devuelve nullptr si no es N1 a N5.
 */
const Form* find_form(const std::string& id) {
    for (const Form& f : kForms) {
        if (id == f.id) return &f;
    }
    return nullptr;
}

/*
 * Lee un entero positivo en forma canonica (sin signo ni ceros a la
 * izquierda) y revisa que este entre 1 y max.
 *
 * Errores:
 *   Lanza QueryError si no es valido.
 */
std::int64_t parse_positive(const std::string& text, std::int64_t max, const std::string& what) {
    bool ok = !text.empty() && text.size() <= 18 && text[0] != '0';
    std::int64_t v = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') ok = false;
        if (ok) v = v * 10 + (ch - '0');  // 18 digitos no desbordan int64
    }
    if (!ok || v < 1 || v > max) {
        throw QueryError("'" + text + "' no es valido para " + what + " (entero de 1 a " + std::to_string(max) + ")");
    }
    return v;
}

/*
 * Revisa el prefijo de N1: de 1 a 30 letras ASCII. Sin comodines, comillas
 * ni espacios, asi el valor no puede cambiar la forma del LIKE.
 *
 * Errores:
 *   Lanza QueryError si no es valido.
 */
std::string parse_prefix(const std::string& text) {
    bool ok = !text.empty() && text.size() <= kMaxPrefix;
    for (char ch : text) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) ok = false;
    }
    if (!ok) throw QueryError("el prefijo de N1 debe tener de 1 a 30 letras sin acentos, se recibio '" + text + "'");
    return text;
}

/*
 * Escribe un real como sus 64 bits IEEE 754 (big-endian por u64).
 */
std::uint64_t real_bits(double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    return bits;
}

/*
 * Recupera un real a partir de sus 64 bits.
 */
double bits_real(std::uint64_t bits) {
    double v = 0;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

/*
 * Columna de entero o de texto, para armar las listas de columnas.
 */
UnverifiedColumn integer_column(const char* name) { return {name, UnverifiedType::kInteger}; }
UnverifiedColumn text_column(const char* name) { return {name, UnverifiedType::kText}; }
UnverifiedColumn real_column(const char* name) { return {name, UnverifiedType::kReal}; }

}  // namespace

/*
 * Implementacion: solo la primera palabra.
 */
bool is_unverified_query(const std::vector<std::string>& words) {
    return !words.empty() && find_form(words[0]) != nullptr;
}

/*
 * Implementacion: cada forma tiene exactamente un parametro.
 */
UnverifiedQuery parse_unverified_query(const std::vector<std::string>& words) {
    if (words.empty()) throw QueryError("falta la consulta");
    const Form* form = find_form(words[0]);
    if (form == nullptr) throw QueryError("'" + words[0] + "' no es una consulta no verificable (N1 a N5)");
    if (words.size() != 2) throw QueryError(std::string("la consulta ") + form->id + " se escribe: " + form->usage);

    UnverifiedQuery q;
    q.kind = form->kind;
    q.catalog_id = form->id;
    switch (q.kind) {
        case UnverifiedKind::kSurnamePrefix:
            q.text_value = parse_prefix(words[1]);
            break;
        case UnverifiedKind::kTopAverages:
            q.number = parse_positive(words[1], kMaxTop, "el numero de alumnos");
            break;
        case UnverifiedKind::kAverageByTerm:
        case UnverifiedKind::kMaximum:
        case UnverifiedKind::kFullTranscript:
            q.number = parse_positive(words[1], 999999999999LL, "student_id");
            break;
    }
    return q;
}

/*
 * Implementacion: plantillas fijas con orden explicito, para que la misma
 * base siempre devuelva las filas en el mismo orden.
 */
std::string unverified_sql(const UnverifiedQuery& q) {
    const std::string n = std::to_string(q.number);
    switch (q.kind) {
        case UnverifiedKind::kSurnamePrefix:
            return "SELECT student_id, enrollment_number, first_name, paternal_surname, maternal_surname, "
                   "program_id, admission_term FROM student WHERE paternal_surname LIKE '" +
                   q.text_value +
                   "%' ORDER BY paternal_surname, maternal_surname, first_name, student_id";
        case UnverifiedKind::kAverageByTerm:
            return "SELECT term_code, COUNT(*), AVG(grade) FROM academic_record WHERE student_id = " + n +
                   " GROUP BY term_code ORDER BY term_code";
        case UnverifiedKind::kMaximum:
            return "SELECT MAX(grade) FROM academic_record WHERE student_id = " + n;
        case UnverifiedKind::kFullTranscript:
            return "SELECT s.enrollment_number, p.program_name, r.term_code, c.course_code, c.course_name, "
                   "c.credits_tenths, r.evaluation_type, r.grade FROM student s "
                   "JOIN program p ON p.program_id = s.program_id "
                   "JOIN academic_record r ON r.student_id = s.student_id "
                   "JOIN course c ON c.course_id = r.course_id WHERE s.student_id = " +
                   n + " ORDER BY r.term_code, c.course_code, r.evaluation_type, r.record_id";
        case UnverifiedKind::kTopAverages:
            return "SELECT s.enrollment_number, s.first_name, s.paternal_surname, s.maternal_surname, COUNT(*), "
                   "AVG(r.grade) FROM student s JOIN academic_record r ON r.student_id = s.student_id "
                   "GROUP BY s.student_id, s.enrollment_number, s.first_name, s.paternal_surname, "
                   "s.maternal_surname ORDER BY AVG(r.grade) DESC, s.enrollment_number LIMIT " +
                   n;
    }
    return {};
}

/*
 * Implementacion: el mismo orden que el SELECT de unverified_sql.
 */
std::vector<UnverifiedColumn> unverified_columns(const UnverifiedQuery& q) {
    switch (q.kind) {
        case UnverifiedKind::kSurnamePrefix:
            return {integer_column("student_id"),    integer_column("enrollment_number"),
                    text_column("first_name"),        text_column("paternal_surname"),
                    text_column("maternal_surname"), integer_column("program_id"),
                    integer_column("admission_term")};
        case UnverifiedKind::kAverageByTerm:
            return {integer_column("term_code"), integer_column("COUNT(*)"), real_column("AVG(grade)")};
        case UnverifiedKind::kMaximum:
            return {integer_column("MAX(grade)")};
        case UnverifiedKind::kFullTranscript:
            return {integer_column("student.enrollment_number"), text_column("program.program_name"),
                    integer_column("academic_record.term_code"),  text_column("course.course_code"),
                    text_column("course.course_name"),            integer_column("course.credits_tenths"),
                    integer_column("academic_record.evaluation_type"), integer_column("academic_record.grade")};
        case UnverifiedKind::kTopAverages:
            return {integer_column("student.enrollment_number"), text_column("student.first_name"),
                    text_column("student.paternal_surname"),     text_column("student.maternal_surname"),
                    integer_column("COUNT(*)"),                  real_column("AVG(grade)")};
    }
    return {};
}

/*
 * Implementacion: los motivos coinciden con la justificacion de las
 * consultas excluidas del reporte tecnico.
 */
std::string unverified_reason(const UnverifiedQuery& q) {
    switch (q.kind) {
        case UnverifiedKind::kSurnamePrefix:
            return "LIKE compara texto por patron. El esquema solo ordena y autentica claves enteras, "
                   "asi que no hay un AIT que demuestre que no falta ningun alumno con ese apellido.";
        case UnverifiedKind::kAverageByTerm:
            return "GROUP BY necesita demostrar cada grupo y que no falte ninguno. El esquema autentica "
                   "agregados sobre un solo conjunto (SUM, COUNT, AVG), no un agregado por grupo.";
        case UnverifiedKind::kMaximum:
            return "MAX no esta en el plan de arboles. Su prueba seria simetrica a la de C13 (MIN), "
                   "pero se dejo como trabajo futuro.";
        case UnverifiedKind::kFullTranscript:
            return "El esquema verifica JOIN entre dos tablas (C8, C9). Un JOIN de cuatro tablas "
                   "requeriria componer pruebas de multiconjuntos que IntegriDB no define.";
        case UnverifiedKind::kTopAverages:
            return "Ordenar por un promedio calculado y cortar con LIMIT exige probar el orden de un valor "
                   "derivado de varias filas; el AIT solo ordena columnas almacenadas.";
    }
    return {};
}

/*
 * Implementacion: ver el formato en unverified.hpp.
 */
std::vector<unsigned char> encode_unverified_result(const UnverifiedResult& result) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u8(kVersion).text(result.catalog_id);
    w.u32(static_cast<std::uint32_t>(result.columns.size()));
    for (const UnverifiedColumn& c : result.columns) {
        w.text(c.name).u8(static_cast<std::uint8_t>(c.type));
    }
    w.u64(result.row_count);
    w.u32(static_cast<std::uint32_t>(result.rows.size()));
    for (const std::vector<ResultValue>& row : result.rows) {
        for (const ResultValue& v : row) {
            if (std::holds_alternative<std::monostate>(v)) {
                w.u8(kTagNull);
            } else if (const auto* i = std::get_if<std::int64_t>(&v)) {
                w.u8(static_cast<std::uint8_t>(UnverifiedType::kInteger)).i64(*i);
            } else if (const auto* d = std::get_if<double>(&v)) {
                w.u8(static_cast<std::uint8_t>(UnverifiedType::kReal)).u64(real_bits(*d));
            } else {
                w.u8(static_cast<std::uint8_t>(UnverifiedType::kText)).text(std::get<std::string>(v));
            }
        }
    }
    return w.data();
}

/*
 * Implementacion: lectura estricta campo por campo; cualquier diferencia
 * con la forma esperada es un FormatError.
 */
UnverifiedResult decode_unverified_result(const std::vector<unsigned char>& bytes, const UnverifiedQuery& expected) {
    io::BinaryReader r(bytes.data(), bytes.size());
    if (std::memcmp(r.raw(sizeof kMagic), kMagic, sizeof kMagic) != 0) {
        throw io::FormatError("no es un resultado TTUNV");
    }
    if (r.u8() != kVersion) throw io::FormatError("version de TTUNV no soportada");

    UnverifiedResult out;
    out.catalog_id = r.text(kMaxIdBytes);
    if (out.catalog_id != expected.catalog_id) {
        throw io::FormatError("el resultado corresponde a otra consulta (" + out.catalog_id + ")");
    }
    const std::uint32_t column_count = r.u32();
    if (column_count == 0 || column_count > kMaxColumns) throw io::FormatError("numero de columnas invalido");
    for (std::uint32_t c = 0; c < column_count; ++c) {
        UnverifiedColumn col;
        col.name = r.text(kMaxNameBytes);
        col.type = static_cast<UnverifiedType>(r.u8());
        out.columns.push_back(col);
    }
    if (out.columns != unverified_columns(expected)) {
        throw io::FormatError("las columnas del resultado no son las de " + expected.catalog_id);
    }

    out.row_count = r.u64();
    const std::uint32_t row_total = r.u32();
    if (row_total > out.row_count) throw io::FormatError("hay mas filas que las que declara el resultado");
    // Cada celda ocupa al menos su etiqueta: un contador alterado no reserva memoria.
    r.require_items(static_cast<std::uint64_t>(row_total) * column_count, 1);
    out.rows.reserve(row_total);
    for (std::uint32_t i = 0; i < row_total; ++i) {
        std::vector<ResultValue> row;
        row.reserve(column_count);
        for (const UnverifiedColumn& col : out.columns) {
            const std::uint8_t tag = r.u8();
            if (tag == kTagNull) {
                row.emplace_back(std::monostate{});
            } else if (tag != static_cast<std::uint8_t>(col.type)) {
                throw io::FormatError("una celda no es del tipo de la columna " + col.name);
            } else if (col.type == UnverifiedType::kInteger) {
                row.emplace_back(r.i64());
            } else if (col.type == UnverifiedType::kReal) {
                const double v = bits_real(r.u64());
                if (!std::isfinite(v)) throw io::FormatError("un real del resultado no es finito");
                row.emplace_back(v);
            } else {
                row.emplace_back(r.text(kMaxTextBytes));
            }
        }
        out.rows.push_back(std::move(row));
    }
    r.expect_end();
    return out;
}

}  // namespace tt::query

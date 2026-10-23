// tree_plan.cpp

#include "tt/scheme/tree_plan.hpp"

#include <algorithm>
#include <set>

namespace tt::scheme {

namespace {

/*
 * Atajo para escribir el catalogo: arma un TreeSpec.
 */
TreeSpec tree(const char* table, const char* key, const char* value) { return TreeSpec{table, key, value}; }

/*
 * Arma el catalogo del caso de estudio. Cada consulta sale de la tabla
 * "Relacion entre el modelo de datos y las consultas consideradas" y de los
 * ejemplos de la seccion "Consultas consideradas" del reporte. Los arboles
 * de cada una siguen los procedimientos de IntegriDB:
 *   - seleccion y rango: RangeCover en (columna filtrada, columna de
 *     referencia) da los identificadores de las filas; las filas se
 *     comprueban con su prueba de Merkle (seccion 4.3 del articulo);
 *   - rango en varias columnas: un arbol por columna e interseccion de los
 *     identificadores con el ASO (4.3);
 *   - JOIN: los arboles (i, i) de ambas columnas, cuya raiz acumula los
 *     valores de la columna, e interseccion con el ASO (4.2);
 *   - SUM/AVG: la columna sumada debe ir como valor del arbol (4.4);
 *   - COUNT: el contador de los nodos o el tamano de la interseccion (4.4);
 *   - MIN/MAX: se reducen a un rango con una columna mas (4.4).
 */
std::vector<CatalogQuery> build_catalog() {
    const TreeSpec ar_student = tree("academic_record", "student_id", "record_id");
    const TreeSpec ar_grade = tree("academic_record", "grade", "record_id");
    const TreeSpec ar_term = tree("academic_record", "term_code", "record_id");
    const TreeSpec ar_eval = tree("academic_record", "evaluation_type", "record_id");
    const TreeSpec ar_course_join = tree("academic_record", "course_id", "course_id");
    const TreeSpec ar_student_grade = tree("academic_record", "student_id", "grade");
    const TreeSpec st_enrollment = tree("student", "enrollment_number", "student_id");
    const TreeSpec st_admission = tree("student", "admission_term", "student_id");
    const TreeSpec st_program_join = tree("student", "program_id", "program_id");
    const TreeSpec co_course = tree("course", "course_id", "course_id");
    const TreeSpec pr_program = tree("program", "program_id", "program_id");

    return {
        {"C1", "Seleccion", "Datos de un alumno a partir de su boleta",
         "SELECT * FROM student WHERE enrollment_number = ?",
         {st_enrollment},
         "Search en el AIT da el student_id de esa boleta; la fila se comprueba con su prueba de "
         "Merkle."},
        {"C2", "Seleccion", "Registros academicos de un alumno",
         "SELECT * FROM academic_record WHERE student_id = ?",
         {ar_student},
         "Search da los record_id del alumno y el contador de la hoja fija cuantos son; cada fila "
         "con su prueba de Merkle."},
        {"C3", "Seleccion", "Datos de una unidad de aprendizaje",
         "SELECT * FROM course WHERE course_id = ?",
         {co_course},
         "Search sobre la columna de referencia confirma si existe; la fila con su prueba de "
         "Merkle."},
        {"C4", "Rango", "Registros con calificacion dentro de un intervalo",
         "SELECT * FROM academic_record WHERE grade BETWEEN ? AND ?",
         {ar_grade},
         "RangeCover da los record_id del intervalo (sin omitir ninguno); filas con prueba de "
         "Merkle."},
        {"C5", "Rango", "Registros comprendidos entre dos periodos escolares",
         "SELECT * FROM academic_record WHERE term_code BETWEEN ? AND ?",
         {ar_term},
         "RangeCover da los record_id del intervalo; filas con prueba de Merkle."},
        {"C6", "Rango", "Alumnos con periodo de ingreso dentro de un intervalo",
         "SELECT * FROM student WHERE admission_term BETWEEN ? AND ?",
         {st_admission},
         "RangeCover da los student_id del intervalo; filas con prueba de Merkle."},
        {"C7", "Rango multidimensional", "Registros con calificacion y periodo dentro de intervalos",
         "SELECT * FROM academic_record WHERE grade BETWEEN ? AND ? AND term_code BETWEEN ? AND ?",
         {ar_grade, ar_term},
         "RangeCover en cada columna; el ASO prueba la interseccion de los dos conjuntos de "
         "record_id; filas con prueba de Merkle."},
        {"C8", "JOIN", "Registros academicos con la unidad de aprendizaje evaluada",
         "SELECT * FROM academic_record JOIN course ON academic_record.course_id = course.course_id",
         {ar_course_join, co_course},
         "Los AIT (course_id, course_id) autentican las claves de ambas tablas; "
         "Split ASO y Bezout prueban pertenencia y completitud de las coincidencias, "
         "con multiplicidades; Merkle autentica las filas devueltas."},
        {"C9", "JOIN", "Alumnos con el programa academico al que pertenecen",
         "SELECT * FROM student JOIN program ON student.program_id = program.program_id",
         {st_program_join, pr_program},
         "Los AIT (program_id, program_id) autentican las claves de ambas tablas; "
         "Split ASO y Bezout comprueban la completitud del JOIN muchos-a-uno y "
         "Merkle autentica las filas devueltas."},
        {"C10", "Agregacion", "Promedio de calificaciones de un alumno",
         "SELECT AVG(grade) FROM academic_record WHERE student_id = ?",
         {ar_student_grade},
         "Search acumula las calificaciones del alumno (la columna sumada va como valor); el ASO "
         "prueba la suma y el contador da el numero de registros."},
        {"C11", "Agregacion", "Evaluaciones no acreditadas de un alumno",
         "SELECT COUNT(*) FROM academic_record WHERE student_id = ? AND grade < 6",
         {ar_student, ar_grade},
         "Rango en dos columnas con interseccion de record_id; se cuenta el resultado."},
        {"C12", "Agregacion", "Evaluaciones de un alumno bajo una forma de evaluacion",
         "SELECT COUNT(*) FROM academic_record WHERE student_id = ? AND evaluation_type = ?",
         {ar_student, ar_eval},
         "Rango en dos columnas con interseccion de record_id; se cuenta el resultado."},
        {"C13", "Agregacion", "Calificacion minima o maxima de un alumno",
         "SELECT MIN(grade) FROM academic_record WHERE student_id = ?",
         {ar_student, ar_grade, ar_student_grade},
         "MIN/MAX se reduce a rango: existe un registro del alumno con esa calificacion y ninguno "
         "por debajo (o por encima)."},
        {"C14", "Composicion", "Creditos aprobados de un alumno",
         "SELECT SUM(course.credits_tenths) FROM academic_record JOIN course ON "
         "academic_record.course_id = course.course_id WHERE academic_record.student_id = ? AND "
         "academic_record.grade >= 6",
         {ar_student, ar_grade, co_course},
         "El rango en dos columnas da los record_id aprobados; esos registros se devuelven con su "
         "prueba de Merkle y de ahi salen sus course_id. Search en course(course_id -> course_id) "
         "y la prueba de Merkle de cada fila de course autentican sus creditos; el verificador suma "
         "los creditos de cada unidad tantas veces como registros aprobados tenga, igual que el "
         "JOIN de SQL."},
    };
}

/*
 * Devuelve los ids de las consultas del catalogo que usan el arbol.
 */
std::vector<std::string> queries_using(const TreeSpec& spec) {
    std::vector<std::string> ids;
    for (const CatalogQuery& q : case_study_catalog()) {
        if (std::find(q.trees.begin(), q.trees.end(), spec) != q.trees.end()) ids.push_back(q.id);
    }
    return ids;
}

/*
 * Busca la estructura de una tabla por nombre.
 *
 * Devuelve:
 *   Puntero a la estructura, o nullptr si no esta.
 */
const db::TableSchema* find_schema(const std::vector<db::TableSchema>& schemas, const std::string& table) {
    for (const db::TableSchema& s : schemas) {
        if (s.name == table) return &s;
    }
    return nullptr;
}

}  // namespace

/*
 * Implementacion: el catalogo se construye una sola vez (variable estatica
 * local, segura entre hilos desde C++11).
 */
const std::vector<CatalogQuery>& case_study_catalog() {
    static const std::vector<CatalogQuery> catalog = build_catalog();
    return catalog;
}

/*
 * Implementacion: compara con los dos nombres posibles.
 */
std::optional<Preset> parse_preset(const std::string& name) {
    if (name == "queries") return Preset::kQueries;
    if (name == "full") return Preset::kFull;
    return std::nullopt;
}

/*
 * Implementacion: un nombre fijo por cada valor del enum.
 */
const char* preset_name(Preset preset) { return preset == Preset::kQueries ? "queries" : "full"; }

/*
 * Implementacion: junta los arboles en un std::set (que los deja en orden
 * canonico y sin repetir), los valida y agrega a cada uno las consultas que
 * lo usan.
 */
std::vector<PlannedTree> plan_trees(Preset preset, const std::vector<db::TableSchema>& schemas) {
    std::set<TreeSpec> specs;
    if (preset == Preset::kQueries) {
        for (const CatalogQuery& q : case_study_catalog()) specs.insert(q.trees.begin(), q.trees.end());
    } else {
        for (const db::TableSchema& s : schemas) {
            for (const db::Column& key : s.columns) {
                if (key.type != db::ColumnType::kInteger) continue;
                for (const db::Column& value : s.columns) {
                    if (value.type == db::ColumnType::kInteger) specs.insert(TreeSpec{s.name, key.name, value.name});
                }
            }
        }
    }

    const std::vector<TreeSpec> list(specs.begin(), specs.end());
    try {
        validate_plan(list, schemas);
    } catch (const PlanError& e) {
        if (preset != Preset::kQueries) throw;
        throw PlanError(std::string(e.what()) +
                        ". El plan 'queries' esta definido para la base del caso de estudio "
                        "(academic_record, student, course, program); para otra base usa el plan 'full'");
    }

    std::vector<PlannedTree> plan;
    plan.reserve(list.size());
    for (const TreeSpec& spec : list) plan.push_back(PlannedTree{spec, queries_using(spec)});
    return plan;
}

/*
 * Implementacion: revisa cada arbol contra las estructuras de las tablas y
 * usa un std::set para detectar repetidos.
 */
void validate_plan(const std::vector<TreeSpec>& trees, const std::vector<db::TableSchema>& schemas) {
    std::set<TreeSpec> seen;
    for (const TreeSpec& spec : trees) {
        const db::TableSchema* schema = find_schema(schemas, spec.table);
        if (schema == nullptr) throw PlanError("arbol " + spec.label() + ": la tabla " + spec.table + " no existe");
        for (const std::string* column : {&spec.key_column, &spec.value_column}) {
            const std::optional<std::size_t> index = schema->column_index(*column);
            if (!index) {
                throw PlanError("arbol " + spec.label() + ": la tabla " + spec.table + " no tiene la columna " +
                                *column);
            }
            if (schema->columns[*index].type != db::ColumnType::kInteger) {
                throw PlanError("arbol " + spec.label() + ": la columna " + *column +
                                " es de texto; un AIT solo acepta columnas enteras");
            }
        }
        if (!seen.insert(spec).second) throw PlanError("el arbol " + spec.label() + " esta repetido en el plan");
    }
}

}  // namespace tt::scheme

// tree_plan.hpp
//
// Que arboles AIT construye Setup.
//
// El algoritmo Setup del reporte (y de IntegriDB) construye un AIT por cada
// par de columnas (i, j) de cada tabla, incluido i = j: m^2 arboles por
// tabla. El articulo lo justifica asi: en consultas anidadas cualquier
// columna podria hacer de columna de referencia, y para sumar una columna
// sobre el resultado de un rango esa columna debe ser el valor del arbol.
// Es decir, m^2 es el precio de admitir cualquier consulta.
//
// Nuestro esquema no admite SQL arbitrario: el reporte delimita un
// subconjunto de consultas (seccion "Consultas consideradas") y la
// inicializacion del esquema anticipa que "en una etapa posterior de
// implementacion, esta construccion podria ajustarse para generar
// unicamente las estructuras necesarias segun el conjunto de consultas
// soportadas". Por eso hay dos planes:
//
//   queries  Solo los arboles que usa alguna consulta del catalogo del caso
//            de estudio (case_study_catalog). Cada arbol queda justificado
//            por las consultas que lo usan. Es el plan por defecto.
//   full     Todos los pares de columnas enteras de cada tabla, igual que
//            el algoritmo Setup del reporte (las columnas de texto nunca
//            entran a un AIT; se protegen con el hash de la fila). Sirve
//            para reproducir el algoritmo tal cual y para medir su costo.
//
// Elegir un plan no cambia la seguridad: cada verificacion usa los mismos
// arboles en ambos planes, y la lista de arboles queda comprometida en el
// digest delta. Una consulta que necesite un arbol que no existe no se
// puede verificar y se rechaza como no soportada (RNF05); nunca se acepta
// sin prueba.

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "tt/db/table.hpp"

namespace tt::scheme {

// Identifica un AIT: ait^T_{i x j} sobre la tabla T con la columna i como
// clave (se filtra por ella) y la columna j como valor (se acumula).
struct TreeSpec {
    std::string table;
    std::string key_column;
    std::string value_column;

    /*
     * Compara tabla, columna clave y columna valor.
     */
    bool operator==(const TreeSpec& o) const {
        return table == o.table && key_column == o.key_column && value_column == o.value_column;
    }

    /*
     * Orden canonico: por tabla, luego columna clave, luego columna valor.
     * Es el orden en que los arboles aparecen en el digest.
     */
    bool operator<(const TreeSpec& o) const {
        return std::tie(table, key_column, value_column) < std::tie(o.table, o.key_column, o.value_column);
    }

    /*
     * Texto corto para mensajes, por ejemplo "academic_record(grade -> record_id)".
     */
    std::string label() const { return table + "(" + key_column + " -> " + value_column + ")"; }
};

// Una consulta del catalogo del caso de estudio y los AIT que su
// verificacion necesita.
struct CatalogQuery {
    std::string id;           // C1, C2, ...
    std::string kind;         // tipo de consulta (seleccion, rango, ...)
    std::string description;  // que informacion academica obtiene
    std::string sql;          // forma de la consulta (? = parametro)
    std::vector<TreeSpec> trees;
    std::string how;          // como se verifica con esos arboles
};

// Planes disponibles (ver arriba).
enum class Preset { kQueries, kFull };

// Un arbol del plan y las consultas del catalogo que lo usan.
struct PlannedTree {
    TreeSpec spec;
    std::vector<std::string> used_by;  // ids del catalogo; vacio si ninguna lo usa
};

// Error al armar o validar un plan de arboles.
class PlanError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Catalogo de consultas del caso de estudio: las de la tabla "Relacion
 * entre el modelo de datos y las consultas consideradas" del reporte, con
 * los AIT que usa cada una segun los procedimientos de IntegriDB
 * (seleccion y rango con la columna de referencia como valor, JOIN con los
 * arboles (i, i), suma con la columna sumada como valor, MIN/MAX
 * reducidos a rango).
 *
 * Devuelve:
 *   La lista fija de consultas, de C1 a C14.
 */
const std::vector<CatalogQuery>& case_study_catalog();

/*
 * Convierte el nombre de un plan ("queries" o "full") en el enum.
 *
 * Devuelve:
 *   El plan, o std::nullopt si el nombre no es ninguno de los dos.
 */
std::optional<Preset> parse_preset(const std::string& name);

/*
 * Devuelve el nombre de un plan: "queries" o "full".
 */
const char* preset_name(Preset preset);

/*
 * Arma la lista de arboles de un plan para las tablas dadas y la valida
 * (validate_plan).
 *
 * Con kQueries es la union de los arboles de case_study_catalog; con kFull
 * son todos los pares (i, j) de columnas enteras de cada tabla. En ambos
 * casos cada arbol lleva las consultas del catalogo que lo usan, y la lista
 * sale en orden canonico (TreeSpec::operator<).
 *
 * Parametros:
 *   preset   plan elegido
 *   schemas  estructura de las tablas de la base
 *
 * Devuelve:
 *   Los arboles del plan.
 *
 * Errores:
 *   Lanza PlanError si el plan no se puede aplicar a estas tablas (por
 *   ejemplo, kQueries sobre una base que no es la del caso de estudio).
 */
std::vector<PlannedTree> plan_trees(Preset preset, const std::vector<db::TableSchema>& schemas);

/*
 * Revisa que una lista de arboles se pueda construir sobre estas tablas:
 * que cada tabla y columna exista, que ambas columnas sean enteras (solo
 * los enteros tienen orden y representacion en Z_r) y que ningun arbol se
 * repita.
 *
 * Errores:
 *   Lanza PlanError con el arbol y el problema.
 */
void validate_plan(const std::vector<TreeSpec>& trees, const std::vector<db::TableSchema>& schemas);

}  // namespace tt::scheme

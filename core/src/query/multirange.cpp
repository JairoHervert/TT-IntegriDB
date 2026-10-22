#include "tt/query/multirange.hpp"

#include <algorithm>
#include <limits>
#include <chrono>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <array>
#include <unordered_map>

#include "tt/ait/node.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/db/row_hash.hpp"

namespace tt::query {
namespace {
using Clock = std::chrono::steady_clock;
/*
 * Calcula los segundos transcurridos desde un instante del reloj monotono.
 *
 * Parametros:
 *   t  instante inicial
 *
 * Devuelve:
 *   Duracion transcurrida en segundos.
 */
double elapsed(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}
class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Extrae los nodos kCover de una prueba RangeCover sin alterar su orden.
 * Dicho orden coincide con los grupos de identificadores de tree->range.
 *
 * Parametros:
 *   proof  prueba AIT que contiene entradas de cobertura y otras entradas
 *
 * Devuelve:
 *   Nodos de cobertura del rango en el orden de la prueba.
 */
std::vector<ait::CoverNode> covers(const ait::RangeProof& proof) {
    std::vector<ait::CoverNode> out;
    for (const auto& e : proof.entries) {
        if (e.kind == ait::EntryKind::kCover) out.push_back({e.header, e.payload});
    }
    return out;
}

/*
 * Convierte referencias de filas en elementos del campo usados por el AIT.
 * Cada identificador se acumula como su inverso modular en Z_r.
 *
 * Parametros:
 *   refs  identificadores de filas del nodo o de la respuesta
 *
 * Devuelve:
 *   FieldSet con las representaciones acumulables de los identificadores.
 *
 * Errores:
 *   Propaga los errores de ait::value_to_element para valores no validos.
 */
aso::FieldSet elements(const std::vector<std::int64_t>& refs) {
    aso::FieldSet s;
    s.elements.reserve(refs.size());
    for (std::int64_t v : refs) s.elements.push_back(ait::value_to_element(v));
    return s;
}

/*
 * Comprueba si una clave pertenece al intervalo cerrado de un nodo AIT.
 *
 * Parametros:
 *   h    encabezado autenticado del nodo
 *   key  valor de la columna filtrada
 *
 * Devuelve:
 *   true si min_key <= key <= max_key; false en otro caso.
 */
bool in_node(const ait::NodeHeader& h, std::int64_t key) {
    return h.min_key <= key && key <= h.max_key;
}

/*
 * Obtiene los IDs de filas publicadas cuya clave pertenece a un nodo de
 * cobertura AIT; cada fila se asigna mediante su columna filtrada.
 *
 * Parametros:
 *   rows              filas de la respuesta con sus pruebas Merkle
 *   key_column        posicion de la columna por la que se filtra
 *   h                 encabezado del nodo de cobertura
 *   reference_column  posicion de la columna que identifica cada fila
 *
 * Devuelve:
 *   Identificadores de las filas publicadas que caen en ese nodo.
 */
std::vector<std::int64_t> result_in_node(const std::vector<RowProof>& rows,
                                         std::size_t key_column, const ait::NodeHeader& h,
                                         std::size_t reference_column) {
    std::vector<std::int64_t> ids;
    for (const RowProof& r : rows) {
        if (in_node(h, std::get<std::int64_t>(r.row[key_column]))) {
            ids.push_back(std::get<std::int64_t>(r.row[reference_column]));
        }
    }
    return ids;
}

/*
 * Comprueba la forma soportada para C7 o para el rango de C13:
 * academic_record, rango grade y rango term_code, con limites ordenados.
 *
 * Parametros:
 *   q  consulta multidimensional que se desea procesar
 *
 * Errores:
 *   Lanza UnsupportedQuery si la forma o los rangos son invalidos.
 */
void check_supported_shape(const MultiRangeQuery& q) {
    const bool common = q.first.table == "academic_record" && q.second.table == "academic_record" &&
        q.first.lo <= q.first.hi && q.second.lo <= q.second.hi;
    const bool c7 = q.first.column == "grade" && q.second.column == "term_code";
    const bool c13 = q.first.column == "student_id" && q.first.is_equality() &&
        q.second.column == "grade" && q.second.lo == std::numeric_limits<std::int64_t>::min();
    if (!common || (!c7 && !c13)) {
        throw UnsupportedQuery("interseccion: solo C7 (grade/term_code) o C13 "
                               "(student_id = ? AND grade <= candidato)");
    }
}
}  // namespace

MultiRangeAnswer answer_multirange(const scheme::AuthenticatedDatabase& adb,
                                  const scheme::DatabaseDigest& digest,
                                  const aso::PublicKey& pk, const MultiRangeQuery& q,
                                  db::RowStore& store) {
    check_supported_shape(q);
    MultiRangeAnswer ans;
    const std::array<RangeQuery, 2> queries = {q.first, q.second};
    const auto* tab = adb.find_table(q.first.table);
    if (!tab) throw ProverError("D~ no tiene la tabla academic_record");
    std::array<ait::RangeAnswer, 2> ranged;
    for (int k = 0; k < 2; ++k) {
        ans.trees[k] = selection_tree(queries[k], digest);
        const auto* tree = adb.find_tree(ans.trees[k]);
        if (!tree) throw ProverError("D~ no contiene el AIT " + ans.trees[k].label());
        ranged[k] = tree->range(queries[k].lo, queries[k].hi);
        ans.input_counts[k] = ranged[k].values.size();
        ans.response.ranges[k] = std::move(ranged[k].proof);
    }
    // El orden de salida es el del AIT de calificaciones.
    const std::unordered_set<std::int64_t> second_ids(ranged[1].values.begin(), ranged[1].values.end());
    std::vector<std::int64_t> common;
    for (std::int64_t id : ranged[0].values) {
        if (second_ids.count(id)) common.push_back(id);
    }
    std::vector<db::Row> rows = store.fetch_rows(tab->schema, common);
    if (rows.size() != common.size()) throw ProverError("MySQL omitio filas de la interseccion C7");
    auto& response = ans.response;
    response.state_id = digest.id();
    response.query = q;
    // const std::size_t ref = tab->schema.reference_column;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto position = tab->position_of(common[i]);
        if (!position) throw ProverError("D~ no contiene la fila de la interseccion");
        if (db::row_hash(tab->schema, rows[i]) != tab->rows.leaves()[*position]) ++ans.stale_rows;
        response.rows.push_back({std::move(rows[i]), tab->rows.prove(*position)});
    }

    // Para cada nodo autentico se calcula el complemento D = N \ (N interseccion R).
    // El testigo de split prueba que el resto junto a los IDs publicados
    // reproduce exactamente el acumulador del nodo autenticado por delta.
    std::array<std::vector<aso::FieldSet>, 2> complements;
    const std::unordered_set<std::int64_t> common_ids(common.begin(), common.end());
    for (int k = 0; k < 2; ++k) {
        const auto cover = covers(response.ranges[k]);
        std::size_t offset = 0;
        for (const auto& n : cover) {
            const std::size_t count = static_cast<std::size_t>(n.header.count);
            if (count > ranged[k].values.size() - offset) {
                throw ProverError("el AIT reporta un contador inconsistente");
            }
            std::vector<std::int64_t> rest;
            rest.reserve(count);
            for (std::size_t j = 0; j < count; ++j) {
                std::int64_t id = ranged[k].values[offset + j];
                if (!common_ids.count(id)) rest.push_back(id);
            }
            offset += count;
            complements[k].push_back(elements(rest));
            response.splits[k].push_back(aso::prove_split(complements[k].back(), pk));
        }
        if (offset != ranged[k].values.size()) throw ProverError("AIT: cobertura incompleta");
    }
    // La coprimalidad de cada par de complementos demuestra que no hay
    // ninguna fila que pertenezca a ambos rangos y no figure en R.
    for (const auto& a : complements[0]) {
        for (const auto& b : complements[1]) {
            response.disjoint.push_back(aso::prove_disjoint(a, b, pk));
        }
    }
    return ans;
}

MultiRangeVerification verify_multirange(const scheme::DatabaseDigest& digest,
                                         const aso::PublicKey& pk, const MultiRangeQuery& asked,
                                         const MultiRangeResponse& response) {
    MultiRangeVerification out;
    try {
        check_supported_shape(asked);
        if (response.state_id != digest.id()) throw Rejected("digest de estado distinto");
        if (!(response.query == asked)) throw Rejected("la respuesta corresponde a otra consulta");
        const std::array<RangeQuery, 2> queries = {asked.first, asked.second};
        const scheme::TableDigest* table = digest.find_table(asked.first.table);
        if (!table) throw Rejected("tabla no publicada en delta");
        const auto& schema = table->schema;
        const std::size_t ref = schema.reference_column;
        std::array<std::size_t, 2> cols = {*schema.column_index(queries[0].column),
                                           *schema.column_index(queries[1].column)};
        std::array<ait::RangeVerification, 2> range;
        auto start = Clock::now();
        for (int k = 0; k < 2; ++k) {
            const auto spec = selection_tree(queries[k], digest);
            const auto* tree = digest.find_tree(spec);
            if (!tree) throw Rejected("falta un AIT autenticado");
            range[k] = ait::verify_range(tree->root, queries[k].lo, queries[k].hi, response.ranges[k]);
            if (!range[k].accepted) throw Rejected("AIT " + spec.label() + ": " + range[k].reason);
            out.cover_nodes[k] = range[k].cover.size();
            if (response.splits[k].size() != range[k].cover.size()) {
                throw Rejected("cantidad de testigos ASO distinta de la cobertura del AIT");
            }
        }
        if (response.disjoint.size() != range[0].cover.size() * range[1].cover.size()) {
            throw Rejected("cantidad incorrecta de certificados de coprimalidad ASO");
        }
        if (response.rows.size() > range[0].count || response.rows.size() > range[1].count) {
            throw Rejected("la interseccion no puede ser mayor que un rango autenticado");
        }
        out.range_seconds = elapsed(start);

        start = Clock::now();
        std::unordered_set<std::int64_t> seen;
        for (const auto& rp : response.rows) {
            const crypto::Digest hash = db::row_hash(schema, rp.row);
            const std::int64_t id = std::get<std::int64_t>(rp.row[ref]);
            if (id == 0 || !seen.insert(id).second) throw Rejected("referencia repetida o nula");
            if (!merkle::verify_inclusion(table->rows_root, table->row_count, hash, rp.inclusion)) {
                throw Rejected("fila manipulada o no autenticada por el arbol de Merkle");
            }
            for (int k = 0; k < 2; ++k) {
                const std::int64_t key = std::get<std::int64_t>(rp.row[cols[k]]);
                if (key < queries[k].lo || key > queries[k].hi) {
                    throw Rejected("fila de la respuesta fuera de los rangos de C7");
                }
            }
        }
        out.rows_seconds = elapsed(start);

        start = Clock::now();
        for (int k = 0; k < 2; ++k) {
            // Cada fila debe caer exactamente en un nodo de cobertura de
            // *cada* dimension; el nodo queda fijado por la clave autentica.
            std::size_t assigned = 0;
            for (std::size_t i = 0; i < range[k].cover.size(); ++i) {
                const auto& cover = range[k].cover[i];
                auto refs = result_in_node(response.rows, cols[k], cover.header, ref);
                assigned += refs.size();
                if (refs.size() > cover.header.count) throw Rejected("un nodo tiene mas filas que su contador");
                if (!aso::verify_split(cover.payload.acc_g1, elements(refs), response.splits[k][i], pk)) {
                    throw Rejected("ASO: el resultado y el complemento no reconstruyen un nodo autenticado");
                }
            }
            if (assigned != response.rows.size()) throw Rejected("una fila no corresponde a la cobertura AIT");
        }
        std::size_t i = 0;
        for (const auto& a : response.splits[0]) {
            for (const auto& b : response.splits[1]) {
                if (!aso::verify_disjoint(a, b, response.disjoint[i++])) {
                    throw Rejected("ASO: los complementos comparten identificadores; faltan filas");
                }
            }
        }
        out.aso_seconds = elapsed(start);
        for (const auto& row : response.rows) out.rows.push_back(row.row);
        out.accepted = true;
    } catch (const std::exception& e) {
        out.accepted = false;
        out.reason = e.what();
        out.rows.clear();
    }
    return out;
}
}  // namespace tt::query

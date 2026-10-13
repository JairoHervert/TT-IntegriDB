// selection.cpp

#include "tt/query/selection.hpp"

#include <chrono>
#include <unordered_set>
#include <utility>

#include "tt/ait/node.hpp"
#include "tt/aso/accumulator.hpp"
#include "tt/db/row_hash.hpp"

namespace tt::query {

namespace {

using Clock = std::chrono::steady_clock;

/*
 * Segundos transcurridos desde `start`.
 */
double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/*
 * Excepcion interna para abandonar la verificacion en cuanto algo no cuadra.
 * verify_selection la atrapa y la convierte en un rechazo con su motivo.
 */
class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Texto "<columna> = <valor>" para los mensajes.
 */
std::string cell_text(const db::TableSchema& schema, std::size_t column, std::int64_t value) {
    return schema.columns[column].name + " = " + std::to_string(value);
}

}  // namespace

/*
 * Implementacion: RangeCover en el AIT de la consulta; las filas de esos
 * identificadores salen del origen de filas, en el mismo orden; cada una
 * lleva la prueba de inclusion de su posicion en el arbol de Merkle de la
 * tabla. Se cuentan las filas cuyo hash ya no es la hoja de D~.
 */
SelectionAnswer answer_selection(const scheme::AuthenticatedDatabase& adb, const scheme::DatabaseDigest& digest,
                                 const RangeQuery& query, db::RowStore& store) {
    SelectionAnswer answer;
    answer.tree = selection_tree(query, digest);
    const ait::Tree* tree = adb.find_tree(answer.tree);
    const scheme::AuthenticatedTable* table = adb.find_table(query.table);
    if (tree == nullptr || table == nullptr) {
        throw ProverError("D~ no tiene el arbol " + answer.tree.label() + " que publica delta");
    }

    ait::RangeAnswer range = tree->range(query.lo, query.hi);
    std::vector<db::Row> rows = store.fetch_rows(table->schema, range.values);
    if (rows.size() != range.values.size()) {
        throw ProverError("el origen de filas devolvio " + std::to_string(rows.size()) + " filas y se pidieron " +
                          std::to_string(range.values.size()));
    }

    SelectionResponse& response = answer.response;
    response.state_id = digest.id();
    response.query = query;
    response.range = std::move(range.proof);
    response.rows.reserve(rows.size());
    const std::size_t ref = table->schema.reference_column;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::optional<std::uint64_t> position = table->position_of(range.values[i]);
        if (!position) {
            throw ProverError("D~ no tiene la fila con " + cell_text(table->schema, ref, range.values[i]) +
                              " que indica el AIT");
        }
        if (db::row_hash(table->schema, rows[i]) != table->rows.leaves()[*position]) ++answer.stale_rows;
        response.rows.push_back(RowProof{std::move(rows[i]), table->rows.prove(*position)});
    }
    return answer;
}

/*
 * Implementacion: los pasos de selection.hpp en orden; los acumuladores, que
 * son lo costoso, al final y solo si se pidieron. Cualquier falla abandona
 * la verificacion con su motivo.
 */
SelectionVerification verify_selection(const scheme::DatabaseDigest& digest, const aso::PublicKey& pk,
                                       const RangeQuery& asked, const SelectionResponse& response, Check check) {
    SelectionVerification out;
    try {
        // 1. Estado y consulta.
        if (response.state_id != digest.id()) {
            throw Rejected("la respuesta se genero sobre otro estado de la base (otro digest delta)");
        }
        if (!(response.query == asked)) {
            throw Rejected("la respuesta corresponde a otra consulta: " + to_sql(response.query));
        }
        scheme::TreeSpec spec;
        try {
            spec = selection_tree(asked, digest);
        } catch (const UnsupportedQuery& e) {
            throw Rejected(std::string("consulta no soportada: ") + e.what());
        }
        const scheme::TableDigest& table = *digest.find_table(asked.table);
        const scheme::TreeDigest& tree = *digest.find_tree(spec);
        const db::TableSchema& schema = table.schema;
        const std::size_t key = *schema.column_index(asked.column);
        const std::size_t ref = schema.reference_column;

        // 2 y 3. Prueba del AIT y numero de filas.
        auto start = Clock::now();
        const ait::RangeVerification range = ait::verify_range(tree.root, asked.lo, asked.hi, response.range);
        if (!range.accepted) throw Rejected("prueba del AIT " + spec.label() + ": " + range.reason);
        if (range.count != response.rows.size()) {
            throw Rejected("el AIT autenticado indica " + std::to_string(range.count) +
                           " filas en el rango y la respuesta trae " + std::to_string(response.rows.size()));
        }
        out.times.range = seconds_since(start);

        // 4. Cada fila: forma, sin repetir y con prueba de Merkle valida.
        start = Clock::now();
        std::vector<std::int64_t> refs(response.rows.size());
        std::vector<std::int64_t> keys(response.rows.size());
        std::unordered_set<std::int64_t> seen;
        seen.reserve(response.rows.size());
        for (std::size_t i = 0; i < response.rows.size(); ++i) {
            const RowProof& r = response.rows[i];
            crypto::Digest hash;
            try {
                hash = db::row_hash(schema, r.row);  // tambien revisa numero y tipo de las celdas
            } catch (const db::DataError& e) {
                throw Rejected("la fila " + std::to_string(i + 1) + " no tiene la estructura de la tabla: " + e.what());
            }
            refs[i] = std::get<std::int64_t>(r.row[ref]);
            keys[i] = std::get<std::int64_t>(r.row[key]);
            if (!seen.insert(refs[i]).second) {
                throw Rejected("la fila con " + cell_text(schema, ref, refs[i]) + " aparece mas de una vez");
            }
            if (!merkle::verify_inclusion(table.rows_root, table.row_count, hash, r.inclusion)) {
                throw Rejected("la fila con " + cell_text(schema, ref, refs[i]) +
                               " no es autentica: su prueba de Merkle no lleva a la raiz de " + schema.name +
                               " en delta (la fila fue alterada o no existe)");
            }
        }

        // 5. A cada nodo de cobertura, tantas filas como su contador, todas
        //    con la clave dentro de su intervalo.
        std::size_t next = 0;
        for (const ait::CoverNode& node : range.cover) {
            const ait::NodeHeader& h = node.header;
            for (std::uint64_t j = 0; j < h.count; ++j, ++next) {
                if (keys[next] < h.min_key || keys[next] > h.max_key) {
                    throw Rejected("la fila con " + cell_text(schema, ref, refs[next]) + " tiene " +
                                   cell_text(schema, key, keys[next]) + ", fuera del intervalo [" +
                                   std::to_string(h.min_key) + ", " + std::to_string(h.max_key) +
                                   "] del nodo de cobertura que le corresponde: la respuesta omite, agrega o "
                                   "cambia filas");
                }
            }
        }
        out.times.rows = seconds_since(start);

        // Opcional: los identificadores de cada nodo contra su acumulador (ASO).
        if (check == Check::kWithAccumulators) {
            start = Clock::now();
            next = 0;
            for (const ait::CoverNode& node : range.cover) {
                const ait::NodeHeader& h = node.header;
                if (h.count > pk.q()) throw Rejected("un nodo de cobertura tiene mas filas que q");
                aso::FieldSet elements;
                elements.elements.reserve(static_cast<std::size_t>(h.count));
                for (std::uint64_t j = 0; j < h.count; ++j, ++next) {
                    if (refs[next] == 0) throw Rejected("una fila tiene referencia 0, que ningun AIT puede acumular");
                    elements.elements.push_back(ait::value_to_element(refs[next]));
                }
                if (!(aso::accumulate_g1(elements, pk) == node.payload.acc_g1)) {
                    throw Rejected("las filas con " + schema.columns[key].name + " en [" + std::to_string(h.min_key) +
                                   ", " + std::to_string(h.max_key) +
                                   "] no son las del acumulador autenticado de ese nodo: la respuesta omite, "
                                   "agrega o cambia filas");
                }
            }
            out.times.accumulators = seconds_since(start);
        }

        out.cover_nodes = range.cover.size();
        out.rows.reserve(response.rows.size());
        for (const RowProof& r : response.rows) out.rows.push_back(r.row);
        out.accepted = true;
    } catch (const Rejected& r) {
        const VerificationTimes times = out.times;
        out = SelectionVerification{};  // nada de lo recibido queda como verificado
        out.reason = r.what();
        out.times = times;
    }
    return out;
}

}  // namespace tt::query

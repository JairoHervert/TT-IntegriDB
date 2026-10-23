#include "join_common.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "tt/ait/node.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/db/row_hash.hpp"
#include "tt/merkle/tree.hpp"
#include "tt/query/query.hpp"

namespace tt::query {
namespace {
constexpr std::int64_t kLo = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kHi = std::numeric_limits<std::int64_t>::max();

/*
 * Arma una especificacion (i,i) para la columna JOIN configurada.
 *
 * Parametros:
 *   table   nombre de la tabla autenticada
 *   config  columna y tablas correspondientes a C8 o C9
 *
 * Devuelve:
 *   TreeSpec cuya clave y valor son la columna de union.
 */
scheme::TreeSpec join_tree(const char* table, const JoinConfig& config) {
    return {table, config.key_column, config.key_column};
}

/*
 * Convierte un multiconjunto de claves a elementos del campo ASO, sin
 * eliminar duplicados. Es necesario conservar las multiplicidades SQL.
 *
 * Parametros:
 *   values  valores de la columna JOIN obtenidos del AIT
 *
 * Devuelve:
 *   FieldSet de las mismas claves con representacion criptografica.
 */
aso::FieldSet elements(const std::vector<std::int64_t>& values) {
    aso::FieldSet out;
    out.elements.reserve(values.size());
    for (const auto value : values) out.elements.push_back(ait::value_to_element(value));
    return out;
}

/*
 * Obtiene el acumulador autenticado para el rango total del AIT.
 *
 * Parametros:
 *   verification  RangeCover previamente validado
 *   config        identificador usado en los errores
 *
 * Devuelve:
 *   Acumuladores en G1 y G2 de la raiz; para arbol vacio, g1 y g2.
 *
 * Errores:
 *   Lanza runtime_error si el rango no corresponde a la raiz.
 */
aso::SplitWitness full_accumulator(const ait::RangeVerification& verification,
                                   const JoinConfig& config) {
    const auto& group = crypto::init_pairing_group();
    if (verification.count == 0 && verification.cover.empty()) return {group.g1, group.g2};
    if (verification.cover.size() != 1 || verification.cover[0].header.count != verification.count)
        throw std::runtime_error(std::string(config.query_id) + ": la cobertura completa no es la raiz del AIT");
    return {verification.cover[0].payload.acc_g1, verification.cover[0].payload.acc_g2};
}

/*
 * Construye la prueba Merkle de una fila obtenida de MySQL.
 *
 * Parametros:
 *   tab     tabla autenticada que conserva referencias y hashes
 *   row     fila completa leida desde la base SQL
 *   stale   contador de hashes que difieren de Setup
 *   config  identificador de la consulta
 *
 * Devuelve:
 *   Fila y camino de inclusion Merkle.
 *
 * Errores:
 *   Lanza ProverError si la referencia no esta autenticada.
 */
RowProof prove_row(const scheme::AuthenticatedTable& tab, db::Row row,
                   std::size_t& stale, const JoinConfig& config) {
    const auto ref = std::get<std::int64_t>(row.at(tab.schema.reference_column));
    const auto pos = tab.position_of(ref);
    if (!pos)
        throw ProverError(std::string(config.query_id) +
                          ": fila de MySQL no pertenece al estado autenticado: " + std::to_string(ref));
    if (db::row_hash(tab.schema, row) != tab.rows.leaves().at(*pos)) ++stale;
    return {std::move(row), tab.rows.prove(*pos)};
}

/*
 * Comprueba tipo, referencia unica y prueba Merkle de una fila recibida.
 *
 * Parametros:
 *   tab     digest de la tabla autenticada
 *   rp      fila y prueba Merkle no confiables
 *   seen    referencias previamente verificadas de la misma tabla
 *   config  identificador de la consulta
 *
 * Errores:
 *   Lanza runtime_error ante una fila mal formada, duplicada o alterada.
 */
void verify_row(const scheme::TableDigest& tab, const RowProof& rp,
                std::unordered_set<std::int64_t>& seen, const JoinConfig& config) {
    const auto prefix = std::string(config.query_id) + ": ";
    if (rp.row.size() != tab.schema.columns.size()) throw std::runtime_error(prefix + "cantidad de columnas incorrecta");
    for (std::size_t i = 0; i < rp.row.size(); ++i) {
        const auto type = tab.schema.columns[i].type;
        if ((type == db::ColumnType::kInteger) != std::holds_alternative<std::int64_t>(rp.row[i]))
            throw std::runtime_error(prefix + "tipo de celda incorrecto");
    }
    const auto ref = std::get<std::int64_t>(rp.row.at(tab.schema.reference_column));
    if (!seen.insert(ref).second) throw std::runtime_error(prefix + "referencia repetida");
    if (!merkle::verify_inclusion(tab.rows_root, tab.row_count,
                                  db::row_hash(tab.schema, rp.row), rp.inclusion))
        throw std::runtime_error(prefix + "fila sin prueba de Merkle valida");
}

}  // namespace

JoinCoreAnswer answer_join_common(const scheme::AuthenticatedDatabase& adb,
                                  const scheme::DatabaseDigest& digest,
                                  const aso::PublicKey& pk, db::RowStore& store,
                                  const JoinConfig& config) {
    JoinCoreAnswer out;
    auto& response = out.response;
    response.state_id = digest.id();
    const std::array<const char*, 2> names = {config.left_table, config.right_table};
    const std::array<const scheme::AuthenticatedTable*, 2> tabs = {
        adb.find_table(names[0]), adb.find_table(names[1])};
    std::array<ait::RangeAnswer, 2> ranged;
    std::array<aso::FieldSet, 2> all_fields;
    for (int k = 0; k < 2; ++k) {
        const auto spec = join_tree(names[k], config);
        if (!digest.find_tree(spec)) throw UnsupportedQuery(std::string(config.query_id) + ": falta AIT " + spec.label());
        const auto* tree = adb.find_tree(spec);
        if (!tree || !tabs[k]) throw ProverError(std::string(config.query_id) + ": falta AIT o tabla en D~");
        ranged[k] = tree->range(kLo, kHi);
        response.ranges[k] = std::move(ranged[k].proof);
        all_fields[k] = elements(ranged[k].values);
    }
    const std::unordered_set<std::int64_t> right_keys(ranged[1].values.begin(), ranged[1].values.end());
    const std::unordered_set<std::int64_t> left_keys(ranged[0].values.begin(), ranged[0].values.end());

    // El AIT (i,i) guarda claves de union, no referencias de filas.
    // D~ aporta las referencias necesarias para recuperar las filas; ASO
    // certifica luego que todas las coincidencias quedaron incluidas.
    const auto left_rows = store.fetch_rows(tabs[0]->schema, tabs[0]->references);
    const auto right_rows = store.fetch_rows(tabs[1]->schema, tabs[1]->references);
    const auto left_col = tabs[0]->schema.column_index(config.key_column);
    const auto right_col = tabs[1]->schema.column_index(config.key_column);
    if (!left_col || !right_col)
        throw ProverError(std::string(config.query_id) + ": falta columna de JOIN en una tabla de D~");
    for (const auto& row : left_rows) {
        const auto key = std::get<std::int64_t>(row.at(*left_col));
        if (right_keys.count(key))
            response.rows[0].push_back(prove_row(*tabs[0], row, out.stale_rows, config));
    }
    for (const auto& row : right_rows) {
        const auto key = std::get<std::int64_t>(row.at(*right_col));
        if (left_keys.count(key))
            response.rows[1].push_back(prove_row(*tabs[1], row, out.stale_rows, config));
    }

    // Los complementos se derivan de los AIT autenticados, no de valores
    // potencialmente manipulados en MySQL despues de Setup.
    std::array<std::vector<std::int64_t>, 2> leftovers;
    for (const auto key : ranged[0].values) if (!right_keys.count(key)) leftovers[0].push_back(key);
    for (const auto key : ranged[1].values) if (!left_keys.count(key)) leftovers[1].push_back(key);
    for (int k = 0; k < 2; ++k) response.splits[k] = aso::prove_split(elements(leftovers[k]), pk);
    response.disjoint[0] = aso::prove_disjoint(elements(leftovers[0]), all_fields[1], pk);
    response.disjoint[1] = aso::prove_disjoint(elements(leftovers[1]), all_fields[0], pk);
    return out;
}

JoinCoreVerification verify_join_common(const scheme::DatabaseDigest& digest,
                                        const aso::PublicKey& pk,
                                        const JoinResponseView& response,
                                        const JoinConfig& config) {
    JoinCoreVerification out;
    try {
        const auto prefix = std::string(config.query_id) + ": ";
        if (response.state_id != digest.id()) throw std::runtime_error(prefix + "digest del estado incorrecto");
        const std::array<const char*, 2> names = {config.left_table, config.right_table};
        std::array<ait::RangeVerification, 2> verified;
        std::array<aso::SplitWitness, 2> full;
        std::array<const scheme::TableDigest*, 2> tables;
        for (int k = 0; k < 2; ++k) {
            const auto* tree = digest.find_tree(join_tree(names[k], config));
            tables[k] = digest.find_table(names[k]);
            if (!tree || !tables[k]) throw std::runtime_error(prefix + "falta AIT o tabla autenticada");
            verified[k] = ait::verify_range(tree->root, kLo, kHi, response.ranges[k]);
            if (!verified[k].accepted) throw std::runtime_error(prefix + "RangeCover invalido: " + verified[k].reason);
            full[k] = full_accumulator(verified[k], config);
            if (verified[k].count != tables[k]->row_count)
                throw std::runtime_error(prefix + "cardinalidad del AIT distinta de la tabla autenticada");
        }
        const auto left_col = tables[0]->schema.column_index(config.key_column);
        const auto right_col = tables[1]->schema.column_index(config.key_column);
        if (!left_col || !right_col) throw std::runtime_error(prefix + "falta columna de JOIN");
        std::array<aso::FieldSet, 2> matched;
        std::unordered_set<std::int64_t> seen_left, seen_right;
        std::unordered_map<std::int64_t, std::uint64_t> counts;
        for (const RowProof& r : response.left_rows) {
            verify_row(*tables[0], r, seen_left, config);
            const auto key = std::get<std::int64_t>(r.row.at(*left_col));
            if (key == 0) throw std::runtime_error(prefix + "clave de JOIN 0 fuera de dominio ASO");
            matched[0].elements.push_back(ait::value_to_element(key));
            ++counts[key];
            out.rows[0].push_back(r.row);
        }
        std::unordered_set<std::int64_t> right_unique_keys;
        for (const RowProof& r : response.right_rows) {
            verify_row(*tables[1], r, seen_right, config);
            const auto key = std::get<std::int64_t>(r.row.at(*right_col));
            if (key == 0 || !right_unique_keys.insert(key).second)
                throw std::runtime_error(prefix + "clave derecha de JOIN repetida o igual a cero");
            if (counts.find(key) == counts.end()) throw std::runtime_error(prefix + "fila derecha sin coincidencia");
            matched[1].elements.push_back(ait::value_to_element(key));
            out.rows[1].push_back(r.row);
        }
        for (const auto& count : counts)
            if (!right_unique_keys.count(count.first)) throw std::runtime_error(prefix + "fila izquierda sin coincidencia");

        // Split conserva las multiplicidades de las claves del lado muchos.
        // El acumulador padre proviene del RangeCover autenticado del AIT.
        for (int k = 0; k < 2; ++k) {
            if (matched[k].elements.size() > verified[k].count)
                throw std::runtime_error(prefix + "mas filas que el AIT autenticado");
            if (!aso::verify_split(full[k].rest_g1, matched[k], response.splits[k], pk))
                throw std::runtime_error(prefix + "certificado de separacion ASO invalido");
        }
        // Bezout impide que un elemento del complemento participe de una
        // coincidencia omitida en el otro conjunto autentico.
        if (!aso::verify_disjoint(response.splits[0], full[1], response.disjoint[0]) ||
            !aso::verify_disjoint(response.splits[1], full[0], response.disjoint[1]))
            throw std::runtime_error(prefix + "certificado ASO de completitud del JOIN invalido");
        out.joined_rows = out.rows[0].size();  // Clave de la tabla derecha unica.
        out.accepted = true;
    } catch (const std::exception& e) {
        out = JoinCoreVerification{};
        out.reason = e.what();
    }
    return out;
}

}  // namespace tt::query

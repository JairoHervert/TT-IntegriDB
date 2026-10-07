// setup.cpp

#include "tt/scheme/setup.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <set>
#include <string>
#include <utility>

#include "tt/db/row_hash.hpp"

namespace tt::scheme {

namespace {

using Clock = std::chrono::steady_clock;

/*
 * Segundos transcurridos desde `start`.
 */
double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/*
 * Construye el compromiso de las filas de una tabla: las ordena por la
 * columna de referencia, calcula el hash de cada una y arma el arbol de
 * Merkle con esos hashes como hojas.
 */
AuthenticatedTable build_table(const db::Table& table) {
    const std::size_t ref = table.schema.reference_column;
    std::vector<std::size_t> order(table.rows.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return std::get<std::int64_t>(table.rows[a][ref]) < std::get<std::int64_t>(table.rows[b][ref]);
    });

    AuthenticatedTable out;
    out.schema = table.schema;
    out.references.reserve(order.size());
    std::vector<crypto::Digest> leaves;
    leaves.reserve(order.size());
    for (std::size_t i : order) {
        out.references.push_back(std::get<std::int64_t>(table.rows[i][ref]));
        leaves.push_back(db::row_hash(table.schema, table.rows[i]));
    }
    out.rows = merkle::Tree(std::move(leaves));
    return out;
}

/*
 * Arma el conjunto S^T_{i x j} de un arbol: un par (x_ri, x_rj) por fila.
 */
std::vector<ait::Entry> entries_for(const db::Table& table, const TreeSpec& spec) {
    const std::size_t key = *table.schema.column_index(spec.key_column);
    const std::size_t value = *table.schema.column_index(spec.value_column);
    std::vector<ait::Entry> entries;
    entries.reserve(table.rows.size());
    for (const db::Row& row : table.rows) {
        entries.push_back(ait::Entry{std::get<std::int64_t>(row[key]), std::get<std::int64_t>(row[value])});
    }
    return entries;
}

/*
 * Revisa las condiciones de los datos que dependen de los arboles del plan:
 * tamano de cada tabla contra q y ausencia de ceros en cada columna usada
 * como valor.
 *
 * Errores:
 *   Lanza SetupError con la tabla, la columna y lo que hay que hacer.
 */
void check_tree_conditions(const db::Database& database, const std::vector<TreeSpec>& trees, std::size_t q) {
    std::set<std::string> sized;
    std::set<std::pair<std::string, std::string>> checked;
    for (const TreeSpec& spec : trees) {
        const db::Table& table = *database.find(spec.table);
        if (sized.insert(spec.table).second && table.rows.size() > q) {
            throw SetupError("la tabla " + spec.table + " tiene " + std::to_string(table.rows.size()) +
                             " filas y la clave publica solo admite conjuntos de hasta q = " + std::to_string(q) +
                             " elementos. Genera claves nuevas con 'tt_owner init --q N' con N >= " +
                             std::to_string(table.rows.size()) + " y vuelve a ejecutar setup");
        }
        if (!checked.insert({spec.table, spec.value_column}).second) continue;
        const std::size_t value = *table.schema.column_index(spec.value_column);
        const std::size_t ref = table.schema.reference_column;
        for (const db::Row& row : table.rows) {
            if (std::get<std::int64_t>(row[value]) == 0) {
                throw SetupError("la columna " + spec.table + "." + spec.value_column + " tiene el valor 0 (fila con " +
                                 table.schema.columns[ref].name + " = " +
                                 std::to_string(std::get<std::int64_t>(row[ref])) + "), y el arbol " + spec.label() +
                                 " acumula cada valor como v^{-1}: 0 no tiene inverso en Z_r");
            }
        }
    }
}

}  // namespace

/*
 * Implementacion: valida la base, el plan, las claves y las condiciones de
 * los datos; despues construye los arboles de Merkle de todas las tablas
 * (ordenadas por nombre) y los AIT del plan (en orden canonico), avisando
 * el tiempo de cada uno, y arma delta con las raices resultantes.
 */
SetupResult setup(const db::Database& database, const std::vector<TreeSpec>& trees, const std::string& plan_name,
                  const SecretKey& sk, const PublicKey& pk,
                  const std::function<void(const SetupProgress&)>& progress) {
    try {
        db::validate(database);
    } catch (const db::DataError& e) {
        throw SetupError(std::string("la base no cumple el modelo del esquema: ") + e.what());
    }
    std::vector<db::TableSchema> schemas;
    for (const db::Table& t : database.tables) schemas.push_back(t.schema);
    try {
        validate_plan(trees, schemas);
    } catch (const PlanError& e) {
        throw SetupError(std::string("plan de arboles invalido: ") + e.what());
    }
    if (!db::is_valid_identifier(plan_name)) throw SetupError("nombre de plan invalido: '" + plan_name + "'");
    if (!keys_match(sk, pk)) {
        throw SetupError("la clave publica no corresponde a la clave secreta: usa el public_key.txt que genero "
                         "el mismo 'tt_owner init' que secret_key.txt");
    }
    check_tree_conditions(database, trees, pk.aso.q());

    SetupResult result;
    result.digest.key_fingerprint = public_key_fingerprint(pk);
    result.digest.plan = plan_name;

    std::vector<const db::Table*> tables;
    for (const db::Table& t : database.tables) tables.push_back(&t);
    std::sort(tables.begin(), tables.end(),
              [](const db::Table* a, const db::Table* b) { return a->schema.name < b->schema.name; });
    for (const db::Table* table : tables) {
        const auto start = Clock::now();
        AuthenticatedTable built = build_table(*table);
        SetupProgress p;
        p.stage = SetupProgress::Stage::kRows;
        p.table = table->schema.name;
        p.items = built.rows.size();
        p.seconds = seconds_since(start);
        result.digest.tables.push_back(TableDigest{built.schema, built.rows.size(), built.rows.root()});
        result.database.tables.push_back(std::move(built));
        if (progress) progress(p);
    }

    std::vector<TreeSpec> ordered = trees;
    std::sort(ordered.begin(), ordered.end());
    result.database.trees.reserve(ordered.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const TreeSpec& spec = ordered[i];
        const auto start = Clock::now();
        ait::Tree tree = ait::setup(entries_for(*database.find(spec.table), spec), sk);
        SetupProgress p;
        p.stage = SetupProgress::Stage::kTree;
        p.table = spec.table;
        p.position = i + 1;
        p.total = ordered.size();
        p.items = tree.leaf_count();
        p.nodes = tree.node_count();
        p.seconds = seconds_since(start);
        result.digest.trees.push_back(TreeDigest{spec, tree.leaf_count(), tree.value_count(), tree.root_digest()});
        result.database.trees.push_back(AuthenticatedTree{spec, std::move(tree)});
        p.tree = &result.database.trees.back().spec;
        if (progress) progress(p);
    }

    validate_digest(result.digest);  // defensa: lo que se publica siempre es coherente
    return result;
}

}  // namespace tt::scheme

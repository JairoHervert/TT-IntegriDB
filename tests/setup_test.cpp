// setup_test.cpp
//
// Pruebas del algoritmo Setup sobre una base pequena con el esquema del
// caso de estudio: que delta y D~ sean los correctos, que Setup sea
// deterministico, que las piezas sirvan para verificar respuestas y que
// las condiciones de los datos se revisen antes de construir.

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/ait/range_proof.hpp"
#include "tt/ait/tree.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/db/row_hash.hpp"
#include "tt/merkle/tree.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace {

using namespace tt;
using scheme::Preset;
using scheme::SetupError;
using scheme::TreeSpec;

/*
 * Solo la identificacion de los arboles de un plan.
 */
std::vector<TreeSpec> plan_specs(Preset preset) {
    std::vector<TreeSpec> specs;
    for (const auto& p : scheme::plan_trees(preset, test::case_study_schemas())) specs.push_back(p.spec);
    return specs;
}

/*
 * Busca una tabla de la base de prueba por nombre (para modificarla).
 */
db::Table& table_named(db::Database& d, const std::string& name) {
    for (db::Table& t : d.tables) {
        if (t.schema.name == name) return t;
    }
    throw std::logic_error("tabla de prueba inexistente: " + name);
}

/*
 * Raiz de un arbol en un digest (o un digest en ceros si no esta).
 */
crypto::Digest tree_root(const scheme::DatabaseDigest& d, const TreeSpec& spec) {
    const scheme::TreeDigest* t = d.find_tree(spec);
    return t ? t->root : crypto::Digest{};
}

/*
 * Raiz de Merkle de una tabla en un digest (o ceros si no esta).
 */
crypto::Digest rows_root(const scheme::DatabaseDigest& d, const std::string& table) {
    const scheme::TableDigest* t = d.find_table(table);
    return t ? t->rows_root : crypto::Digest{};
}

}  // namespace

/*
 * Pruebas de Setup: contenido de delta y D~ contra calculos independientes,
 * avisos de tiempo, determinismo, sensibilidad a los datos, uso de D~ y
 * delta para verificar un rango y una fila, plan full, y rechazo de datos o
 * claves que no cumplen las condiciones.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    using test::check;
    using test::check_throws;
    using test::section;

    const scheme::KeyPair keys = scheme::init(64);
    const db::Database base = test::case_study_database(40);
    const std::vector<TreeSpec> queries = plan_specs(Preset::kQueries);

    std::vector<scheme::SetupProgress> events;
    const scheme::SetupResult r = scheme::setup(base, queries, "queries", keys.sk, keys.pk,
                                                [&](const scheme::SetupProgress& p) { events.push_back(p); });
    const scheme::DatabaseDigest& delta = r.digest;

    section("Contenido de delta");
    {
        check(delta.state_version == 1 && delta.plan == "queries", "version 1 del estado y plan anotado");
        check(delta.key_fingerprint == scheme::public_key_fingerprint(keys.pk), "lleva la huella de la clave publica");
        scheme::PublicKey altered = keys.pk;
        altered.aso.g1_s_powers[5] = altered.aso.g1_s_powers[5] + crypto::init_pairing_group().g1;
        scheme::PublicKey altered_g2 = keys.pk;
        altered_g2.aso.g2_s_powers.back() = altered_g2.aso.g2_s_powers.back() + crypto::init_pairing_group().g2;
        check(scheme::public_key_fingerprint(altered) != delta.key_fingerprint &&
                  scheme::public_key_fingerprint(altered_g2) != delta.key_fingerprint,
              "cambiar cualquier potencia de pk (no solo g^s) cambia la huella");
        std::vector<std::string> names;
        for (const auto& t : delta.tables) names.push_back(t.schema.name);
        check(names == std::vector<std::string>{"academic_record", "course", "program", "student"},
              "las cuatro tablas, ordenadas por nombre");
        check(delta.trees.size() == 11, "los 11 arboles del plan queries");
        bool counts = std::all_of(delta.trees.begin(), delta.trees.end(), [&](const scheme::TreeDigest& t) {
            return t.value_count == delta.find_table(t.spec.table)->row_count;
        });
        check(counts, "cada arbol tiene un valor por cada fila de su tabla");
        scheme::validate_digest(delta);
        check(true, "delta es coherente");
    }

    section("Raices contra calculos independientes");
    {
        bool trees_ok = true;
        for (const TreeSpec& spec : queries) {
            const db::Table& t = *base.find(spec.table);
            const std::size_t k = *t.schema.column_index(spec.key_column);
            const std::size_t v = *t.schema.column_index(spec.value_column);
            std::vector<ait::Entry> entries;
            for (const db::Row& row : t.rows) entries.push_back({std::get<std::int64_t>(row[k]), std::get<std::int64_t>(row[v])});
            trees_ok = trees_ok && ait::setup(entries, keys.sk).root_digest() == tree_root(delta, spec);
        }
        check(trees_ok, "cada raiz de AIT en delta es la de AITSetup sobre S^T_{i x j}");

        bool rows_ok = true;
        for (const db::Table& t : base.tables) {
            std::vector<db::Row> rows = t.rows;
            const std::size_t ref = t.schema.reference_column;
            std::sort(rows.begin(), rows.end(), [ref](const db::Row& a, const db::Row& b) {
                return std::get<std::int64_t>(a[ref]) < std::get<std::int64_t>(b[ref]);
            });
            std::vector<crypto::Digest> leaves;
            for (const db::Row& row : rows) leaves.push_back(db::row_hash(t.schema, row));
            rows_ok = rows_ok && merkle::Tree(leaves).root() == rows_root(delta, t.schema.name);
        }
        check(rows_ok, "cada raiz de filas es el Merkle de los hashes de fila ordenados por referencia");
    }

    section("Avisos de tiempo (RF16)");
    {
        const auto rows = std::count_if(events.begin(), events.end(), [](const scheme::SetupProgress& p) {
            return p.stage == scheme::SetupProgress::Stage::kRows;
        });
        bool numbered = true;
        std::size_t next = 1;
        for (const auto& p : events) {
            if (p.stage != scheme::SetupProgress::Stage::kTree) continue;
            numbered = numbered && p.position == next++ && p.total == 11 && p.tree != nullptr && p.seconds >= 0 &&
                       p.nodes == 2 * p.items - 1;
        }
        check(rows == 4 && next == 12, "un aviso por tabla y uno por arbol");
        check(numbered, "los avisos de arbol van numerados, con hojas, nodos y tiempo");
    }

    section("Determinismo y sensibilidad a los datos");
    {
        db::Database shuffled = base;
        std::mt19937 rng(7);
        for (db::Table& t : shuffled.tables) std::shuffle(t.rows.begin(), t.rows.end(), rng);
        std::reverse(shuffled.tables.begin(), shuffled.tables.end());
        const scheme::SetupResult again = scheme::setup(shuffled, queries, "queries", keys.sk, keys.pk);
        check(again.digest.id() == delta.id(), "mismo delta con las filas y tablas en otro orden (RNF04)");

        db::Database renamed = base;
        std::get<std::string>(table_named(renamed, "student").rows[0][2]) = "Ana Maria";
        const scheme::DatabaseDigest d2 = scheme::setup(renamed, queries, "queries", keys.sk, keys.pk).digest;
        bool trees_same = true;
        for (const TreeSpec& s : queries) trees_same = trees_same && tree_root(d2, s) == tree_root(delta, s);
        check(rows_root(d2, "student") != rows_root(delta, "student") && trees_same,
              "cambiar un nombre (texto) cambia solo la raiz de filas de student");

        db::Database regraded = base;
        db::Row& first = table_named(regraded, "academic_record").rows[0];
        std::get<std::int64_t>(first[5]) = std::get<std::int64_t>(first[5]) == 10 ? 9 : 10;
        const scheme::DatabaseDigest d3 = scheme::setup(regraded, queries, "queries", keys.sk, keys.pk).digest;
        check(rows_root(d3, "academic_record") != rows_root(delta, "academic_record") &&
                  tree_root(d3, {"academic_record", "grade", "record_id"}) !=
                      tree_root(delta, {"academic_record", "grade", "record_id"}) &&
                  tree_root(d3, {"academic_record", "student_id", "grade"}) !=
                      tree_root(delta, {"academic_record", "student_id", "grade"}) &&
                  tree_root(d3, {"academic_record", "term_code", "record_id"}) ==
                      tree_root(delta, {"academic_record", "term_code", "record_id"}),
              "cambiar una calificacion cambia sus filas y los arboles con grade, y no los demas");
        check(d3.id() != delta.id(), "cualquier cambio da otro id de estado");
    }

    section("D~ y delta sirven para verificar");
    {
        const TreeSpec by_grade{"academic_record", "grade", "record_id"};
        const ait::Tree* tree = r.database.find_tree(by_grade);
        const ait::RangeAnswer answer = tree->range(6, 10);
        const ait::RangeVerification v = ait::verify_range(delta.find_tree(by_grade)->root, 6, 10, answer.proof);
        const db::Table& records = *base.find("academic_record");
        std::size_t passed = 0;
        for (const db::Row& row : records.rows) passed += std::get<std::int64_t>(row[5]) >= 6 ? 1 : 0;
        check(v.accepted && answer.values.size() == passed && v.count == passed,
              "RangeCover de calificaciones 6 a 10 se verifica contra la raiz publicada");

        const scheme::AuthenticatedTable& table = *r.database.find_table("academic_record");
        const scheme::TableDigest& published = *delta.find_table("academic_record");
        bool rows_ok = true, tampered_rejected = true;
        for (ait::Value id : answer.values) {
            const db::Row* row = nullptr;
            for (const db::Row& candidate : records.rows) {
                if (std::get<std::int64_t>(candidate[0]) == id) row = &candidate;
            }
            const auto position = table.position_of(id);
            const merkle::InclusionProof proof = table.rows.prove(*position);
            rows_ok = rows_ok && row != nullptr && std::get<std::int64_t>((*row)[5]) >= 6 &&
                      merkle::verify_inclusion(published.rows_root, published.row_count,
                                               db::row_hash(table.schema, *row), proof);
            db::Row altered = *row;
            std::get<std::int64_t>(altered[5]) = 5;
            tampered_rejected = tampered_rejected &&
                                !merkle::verify_inclusion(published.rows_root, published.row_count,
                                                          db::row_hash(table.schema, altered), proof);
        }
        check(rows_ok, "cada fila del resultado se comprueba con su prueba de Merkle");
        check(tampered_rejected, "una fila con la calificacion alterada se rechaza");
        check(!table.position_of(1000).has_value(), "una referencia inexistente no tiene posicion");
    }

    section("Plan full");
    {
        const scheme::SetupResult full = scheme::setup(base, plan_specs(Preset::kFull), "full", keys.sk, keys.pk);
        check(full.digest.trees.size() == 57, "construye los 57 arboles");
        bool shared_same = true;
        for (const TreeSpec& s : queries) shared_same = shared_same && tree_root(full.digest, s) == tree_root(delta, s);
        check(shared_same, "los arboles que comparte con queries son identicos (misma seguridad)");
        check(full.digest.tables == delta.tables, "las filas se comprometen igual en ambos planes");
    }

    section("Condiciones revisadas antes de construir");
    {
        const scheme::KeyPair small = scheme::init(16);
        check_throws<SetupError>([&] { scheme::setup(base, queries, "queries", small.sk, small.pk); },
                                 "una tabla con mas filas que q se rechaza");

        db::Database zero_grade = base;
        std::get<std::int64_t>(table_named(zero_grade, "academic_record").rows[2][5]) = 0;
        check_throws<SetupError>([&] { scheme::setup(zero_grade, queries, "queries", keys.sk, keys.pk); },
                                 "un 0 en una columna usada como valor (grade en student_id -> grade) se rechaza");
        db::Database zero_credits = base;
        std::get<std::int64_t>(table_named(zero_credits, "course").rows[2][3]) = 0;
        scheme::setup(zero_credits, queries, "queries", keys.sk, keys.pk);
        check(true, "un 0 en credits_tenths no importa con queries: ningun arbol acumula esa columna");
        check_throws<SetupError>(
            [&] { scheme::setup(zero_credits, plan_specs(Preset::kFull), "full", keys.sk, keys.pk); },
            "el mismo 0 si se rechaza con full, que acumula todas las columnas enteras");

        const scheme::KeyPair other = scheme::init(64);
        check_throws<SetupError>([&] { scheme::setup(base, queries, "queries", keys.sk, other.pk); },
                                 "una clave publica de otro par se rechaza");
        check_throws<SetupError>(
            [&] { scheme::setup(base, {{"course", "course_code", "course_id"}}, "custom", keys.sk, keys.pk); },
            "un arbol con una columna de texto se rechaza");
        db::Database duplicated = base;
        table_named(duplicated, "program").rows[1][0] = test::int_cell(1);
        check_throws<SetupError>([&] { scheme::setup(duplicated, queries, "queries", keys.sk, keys.pk); },
                                 "una referencia repetida se rechaza");
        check_throws<SetupError>([&] { scheme::setup(base, queries, "no valido", keys.sk, keys.pk); },
                                 "un nombre de plan invalido se rechaza");
    }

    return test::finish();
}

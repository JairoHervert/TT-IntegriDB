// ait_test.cpp
//
// Pruebas del Arbol de Intervalos Autenticado: estructura (contra las
// figuras del reporte), valores de los nodos, RangeCover honesto contra
// fuerza bruta y ataques de un probador malicioso.

#include <NTL/ZZ_p.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "tt/ait/range_proof.hpp"
#include "tt/ait/tree.hpp"
#include "tt/aso/accumulator.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/crypto/random.hpp"
#include "tt/crypto/symmetric.hpp"
#include "tt/scheme/keys.hpp"

namespace {

using namespace tt;
using ait::Entry;
using ait::EntryKind;
using ait::Key;
using ait::Value;
using test::check;
using test::check_throws;
using test::section;

// Claves de cada nivel del arbol, de la raiz hacia abajo (recorrido por niveles).
std::vector<std::vector<Key>> keys_by_level(const ait::Tree& tree) {
    std::vector<std::vector<Key>> levels;
    std::queue<std::pair<std::int32_t, std::size_t>> pending;
    if (!tree.empty()) pending.push({tree.root(), 0});
    while (!pending.empty()) {
        const auto [index, depth] = pending.front();
        pending.pop();
        const ait::Node& n = tree.nodes()[static_cast<std::size_t>(index)];
        if (levels.size() <= depth) levels.emplace_back();
        levels[depth].push_back(n.header.split_key);
        if (!n.header.is_leaf) {
            pending.push({n.left, depth + 1});
            pending.push({n.right, depth + 1});
        }
    }
    return levels;
}

// Valores (ordenados por clave y luego por valor) de las filas con clave en [lo, hi].
std::vector<Value> brute_force(std::vector<Entry> entries, Key lo, Key hi) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.key < b.key || (a.key == b.key && a.value < b.value);
    });
    std::vector<Value> out;
    for (const Entry& e : entries) {
        if (lo <= e.key && e.key <= hi) out.push_back(e.value);
    }
    return out;
}

// e = prod (v^{-1} + s) sobre los valores dados.
NTL::ZZ_p exponent_of(const std::vector<Value>& values, const NTL::ZZ_p& s) {
    NTL::ZZ_p e(1);
    for (Value v : values) e *= ait::value_to_element(v) + s;
    return e;
}

aso::FieldSet inverses(const std::vector<Value>& values) {
    aso::FieldSet set;
    for (Value v : values) set.elements.push_back(ait::value_to_element(v));
    return set;
}

// Valores del subarbol con raiz en `index`.
void subtree_values(const ait::Tree& tree, std::int32_t index, std::vector<Value>& out) {
    const ait::Node& n = tree.nodes()[static_cast<std::size_t>(index)];
    if (n.header.is_leaf) {
        out.insert(out.end(), n.values.begin(), n.values.end());
        return;
    }
    subtree_values(tree, n.left, out);
    subtree_values(tree, n.right, out);
}

std::size_t ceil_log2(std::size_t n) {
    std::size_t bits = 0;
    while ((std::size_t{1} << bits) < n) ++bits;
    return bits;
}

// Replica de single_d_verify del IntegriDB original (common_functions.cpp:28),
// escrita con nuestras primitivas. Recibe los digests de los nodos que manda
// el servidor (bi_digest), la cadena de uniones (bi_proof) y el resultado.
// Notese que no recibe el digest delta: no hay nada que ate los nodos al
// estado autenticado de la base.
bool original_single_d_verify(const std::vector<bn::Ec1>& node_g1, const std::vector<bn::Ec2>& node_g2,
                              const std::vector<bn::Ec1>& bi_proof, const std::vector<aso::Element>& result,
                              const aso::PublicKey& pk, const crypto::PairingGroup& group) {
    using crypto::pairing;
    if (!(pairing(node_g1[0], node_g2[1]) == pairing(bi_proof[0], group.g2))) return false;
    for (std::size_t i = 0; i + 1 < bi_proof.size(); ++i) {
        if (!(pairing(bi_proof[i], node_g2[i + 2]) == pairing(bi_proof[i + 1], group.g2))) return false;
    }
    return aso::accumulate_g1(result, pk) == bi_proof.back();  // compute_digest_pub(result)
}

// Indice de la primera entrada de cobertura interna de la prueba (-1 si no hay).
long first_internal_cover(const ait::RangeProof& proof) {
    for (std::size_t i = 0; i < proof.entries.size(); ++i) {
        const ait::ProofEntry& e = proof.entries[i];
        if (e.kind == EntryKind::kCover && !e.header.is_leaf) return static_cast<long>(i);
    }
    return -1;
}

bool rejected(const ait::Digest& root, Key lo, Key hi, const ait::RangeProof& proof) {
    return !ait::verify_range(root, lo, hi, proof).accepted;
}

}  // namespace

int main() {
    const crypto::PairingGroup& group = crypto::init_pairing_group();
    const scheme::KeyPair keys = scheme::init(256);
    const scheme::SecretKey& sk = keys.sk;
    const aso::PublicKey& pk = keys.pk.aso;
    const NTL::ZZ_p& s = sk.aso.s;

    // -----------------------------------------------------------------------
    section("Estructura: arboles de las figuras del reporte");
    {
        // Tabla del ejemplo ilustrativo: c1 = clave, c2 = valor (x_i2 = i).
        const std::vector<Entry> example = {{24, 1}, {59, 2}, {47, 3}, {11, 4},
                                            {13, 5}, {36, 6}, {19, 7}, {27, 8}};
        const ait::Tree tree = ait::setup(example, sk);
        const auto levels = keys_by_level(tree);
        check(levels.size() == 4 && levels[0] == std::vector<Key>{24} &&
                  levels[1] == std::vector<Key>{13, 36} &&
                  levels[2] == std::vector<Key>{11, 19, 27, 47} &&
                  levels[3] == std::vector<Key>{11, 13, 19, 24, 27, 36, 47, 59},
              "ejemplo ilustrativo: raiz 24; nivel 1: 13, 36; nivel 2: 11, 19, 27, 47 (figura del AIT)");
        check(tree.leaf_count() == 8 && tree.node_count() == 15 && tree.value_count() == 8,
              "8 hojas, 15 nodos, 8 filas");

        // Caso de estudio: enrollment_number (b_1..b_8) como clave, program_id como valor.
        const std::vector<Entry> students = {{2024001, 5001}, {2024002, 5002}, {2024003, 5001},
                                             {2024004, 5003}, {2024005, 5002}, {2024006, 5001},
                                             {2024007, 5003}, {2024008, 5002}};
        const ait::Tree st = ait::setup(students, sk);
        const auto sl = keys_by_level(st);
        check(sl[0] == std::vector<Key>{2024004} && sl[1] == std::vector<Key>{2024002, 2024006} &&
                  sl[2] == std::vector<Key>{2024001, 2024003, 2024005, 2024007},
              "caso de estudio: raiz b4; nivel 1: b2, b6; nivel 2: b1, b3, b5, b7 (figura del caso)");
        const ait::RangeAnswer b5 = st.search(2024005);
        check(b5.values == std::vector<Value>{5002} &&
                  ait::verify_range(st.root_digest(), 2024005, 2024005, b5.proof).accepted,
              "Search(b5) devuelve program_id 5002 y la prueba se verifica (ejemplo del reporte)");
    }

    // -----------------------------------------------------------------------
    section("Valores de los nodos: v_u = Enc(e_u) || g^{e_u}, e_u = prod (v^{-1} + s)");
    const std::vector<Entry> sample = {{8, 11}, {8, 12}, {9, 13}, {10, 14}, {10, 15}, {10, 16},
                                       {6, 17}, {7, 18}, {5, 19}, {7, 20}};
    const ait::Tree sample_tree = ait::setup(sample, sk);
    {
        bool exponents_ok = true;
        bool accumulators_ok = true;
        bool prover_view_ok = true;
        for (std::size_t i = 0; i < sample_tree.node_count(); ++i) {
            const ait::Node& n = sample_tree.nodes()[i];
            std::vector<Value> values;
            subtree_values(sample_tree, static_cast<std::int32_t>(i), values);
            const NTL::ZZ_p e = exponent_of(values, s);
            exponents_ok = exponents_ok && ait::decrypt_exponent(n.payload, sk) == e;
            accumulators_ok = accumulators_ok && n.payload.acc_g1 == group.g1 * crypto::to_vuint(e) &&
                              n.payload.acc_g2 == group.g2 * crypto::to_vuint(e);
            prover_view_ok = prover_view_ok && aso::accumulate_g1(inverses(values), pk) == n.payload.acc_g1;
        }
        check(exponents_ok, "en cada nodo, descifrar Enc(e_u) da prod (v^{-1} + s) de su subarbol");
        check(accumulators_ok, "en cada nodo, los acumuladores son g1^{e_u} y g2^{e_u}");
        check(prover_view_ok, "el acumulador de cada nodo coincide con el que calcula el probador con pk");

        ait::NodePayload tampered = sample_tree.nodes()[0].payload;
        tampered.enc[20] ^= 0x01;
        check_throws<crypto::DecryptionError>([&] { ait::decrypt_exponent(tampered, sk); },
                                              "un cifrado alterado se detecta al descifrar");
    }

    section("Claves duplicadas (una hoja por clave, con contador)");
    {
        check(sample_tree.leaf_count() == 6 && sample_tree.value_count() == 10,
              "10 filas con 6 claves distintas dan 6 hojas");
        const ait::RangeAnswer ten = sample_tree.search(10);
        check(ten.values == std::vector<Value>{14, 15, 16}, "Search(10) devuelve los tres valores de la clave 10");
        const ait::RangeVerification v = ait::verify_range(sample_tree.root_digest(), 10, 10, ten.proof);
        check(v.accepted && v.count == 3 && v.cover.size() == 1 && v.cover[0].header.is_leaf,
              "la hoja de la clave 10 tiene contador 3 y la prueba se verifica");
        check(v.accepted && v.cover[0].payload.acc_g1 == aso::accumulate_g1(inverses({14, 15, 16}), pk),
              "su acumulador es el del multiconjunto {14^-1, 15^-1, 16^-1}");
    }

    section("Determinismo y cambios en los datos");
    {
        std::vector<Entry> shuffled = sample;
        std::reverse(shuffled.begin(), shuffled.end());
        check(ait::setup(shuffled, sk).root_digest() == sample_tree.root_digest(),
              "mismas filas en otro orden: mismo digest");
        check(ait::setup(sample, sk).root_digest() == sample_tree.root_digest(),
              "construir dos veces: mismo digest (experimentos reproducibles)");
        const scheme::KeyPair other_keys = scheme::init(4);
        check(!(ait::setup(sample, other_keys.sk).root_digest() == sample_tree.root_digest()),
              "otras claves: otro digest");
        std::vector<Entry> changed = sample;
        changed[3].value = 99;
        check(!(ait::setup(changed, sk).root_digest() == sample_tree.root_digest()),
              "cambiar un valor cambia el digest");
        std::vector<Entry> more = sample;
        more.push_back({8, 21});
        check(!(ait::setup(more, sk).root_digest() == sample_tree.root_digest()),
              "agregar una fila cambia el digest");
        check_throws<std::invalid_argument>([&] { ait::setup({{1, 0}}, sk); },
                                            "un valor 0 (sin inverso) se rechaza en Setup");
        check_throws<std::invalid_argument>([&] { sample_tree.range(5, 4); }, "un rango con lo > hi se rechaza");
    }

    section("Casos limite");
    {
        const ait::Tree empty = ait::setup({}, sk);
        const ait::RangeAnswer a = empty.range(0, 100);
        const ait::RangeVerification v = ait::verify_range(empty.root_digest(), 0, 100, a.proof);
        check(empty.empty() && empty.root_digest() == ait::empty_tree_hash() && a.values.empty() &&
                  v.accepted && v.count == 0,
              "arbol sin filas: resultado vacio que se verifica");

        const ait::Tree single = ait::setup({{7, 3}}, sk);
        const ait::RangeAnswer in = single.range(0, 10);
        const ait::RangeAnswer out = single.range(8, 10);
        check(in.values == std::vector<Value>{3} && out.values.empty() &&
                  ait::verify_range(single.root_digest(), 0, 10, in.proof).accepted &&
                  ait::verify_range(single.root_digest(), 8, 10, out.proof).accepted,
              "arbol de una sola hoja: rango que la incluye y rango que no");

        check(!ait::verify_range(sample_tree.root_digest(), 0, 100, a.proof).accepted,
              "una prueba vacia no sirve para un arbol con filas");
    }

    // -----------------------------------------------------------------------
    section("RangeCover honesto: 500 rangos aleatorios contra fuerza bruta");
    std::mt19937_64 rng(20261115);  // semilla fija: la prueba es reproducible
    std::vector<Entry> rows;
    for (Value id = 1; id <= 200; ++id) {
        rows.push_back({static_cast<Key>(rng() % 120), id});  // claves repetidas, como grade o term_code
    }
    const ait::Tree tree = ait::setup(rows, sk);
    {
        std::vector<std::pair<Key, Key>> ranges = {{-1000, 1000}, {0, 119}, {-10, -1}, {120, 500},
                                                   {50, 50}, {0, 0}, {119, 119}};
        for (int i = 0; i < 493; ++i) {
            Key a = static_cast<Key>(rng() % 140) - 10;
            Key b = static_cast<Key>(rng() % 140) - 10;
            ranges.push_back({std::min(a, b), std::max(a, b)});
        }
        const std::size_t log_leaves = ceil_log2(tree.leaf_count());
        bool values_ok = true, accepted_ok = true, count_ok = true, order_ok = true;
        bool cover_acc_ok = true, cover_size_ok = true;
        std::size_t max_cover = 0, max_entries = 0;
        for (const auto& [lo, hi] : ranges) {
            const ait::RangeAnswer answer = tree.range(lo, hi);
            const std::vector<Value> expected = brute_force(rows, lo, hi);
            values_ok = values_ok && answer.values == expected;
            const ait::RangeVerification v = ait::verify_range(tree.root_digest(), lo, hi, answer.proof);
            accepted_ok = accepted_ok && v.accepted;
            count_ok = count_ok && v.count == expected.size();
            for (std::size_t i = 0; i < v.cover.size(); ++i) {
                const ait::NodeHeader& h = v.cover[i].header;
                order_ok = order_ok && lo <= h.min_key && h.max_key <= hi &&
                           (i == 0 || v.cover[i - 1].header.max_key < h.min_key);
                const NTL::ZZ_p e = exponent_of(brute_force(rows, h.min_key, h.max_key), s);
                cover_acc_ok = cover_acc_ok && v.cover[i].payload.acc_g1 == group.g1 * crypto::to_vuint(e);
            }
            max_cover = std::max(max_cover, v.cover.size());
            max_entries = std::max(max_entries, answer.proof.entries.size());
            cover_size_ok = cover_size_ok && v.cover.size() <= 2 * log_leaves;
        }
        check(values_ok, "los valores devueltos coinciden con la fuerza bruta en los 500 rangos");
        check(accepted_ok, "todas las pruebas honestas se aceptan");
        check(count_ok, "la suma de contadores de la cobertura es el numero de filas del rango");
        check(order_ok, "los nodos de cobertura estan dentro del rango, ordenados y sin traslaparse");
        check(cover_acc_ok, "cada nodo de cobertura trae el acumulador exacto de las filas que cubre");
        check(cover_size_ok, "la cobertura nunca pasa de 2 log2(n) nodos (tamano logaritmico)");
        std::printf("    (%zu hojas; cobertura maxima %zu nodos; prueba maxima %zu entradas)\n",
                    tree.leaf_count(), max_cover, max_entries);
    }

    // -----------------------------------------------------------------------
    section("Probador malicioso: toda alteracion se rechaza");
    const Key lo = 20;
    const Key hi = 90;
    const ait::RangeAnswer honest = tree.range(lo, hi);
    const ait::Digest& root = tree.root_digest();
    check(ait::verify_range(root, lo, hi, honest.proof).accepted && first_internal_cover(honest.proof) >= 0,
          "(control) la prueba honesta de [20, 90] se acepta");
    {
        const long c = first_internal_cover(honest.proof);
        const std::size_t ci = static_cast<std::size_t>(c);

        ait::RangeProof p = honest.proof;
        p.entries[ci].payload.acc_g1 = p.entries[ci].payload.acc_g1 + group.g1;
        check(rejected(root, lo, hi, p), "cambiar el acumulador G1 de un nodo de cobertura");

        p = honest.proof;
        p.entries[ci].payload.acc_g2 = p.entries[ci].payload.acc_g2 + group.g2;
        check(rejected(root, lo, hi, p), "cambiar el acumulador G2 de un nodo de cobertura");

        p = honest.proof;
        p.entries[ci].header.count -= 1;
        check(rejected(root, lo, hi, p), "cambiar el contador de filas (COUNT falso)");

        // Servidor perezoso: esconde un subarbol del resultado presentandolo como podado.
        p = honest.proof;
        const ait::ProofEntry cover = p.entries[ci];
        ait::ProofEntry hidden;
        hidden.kind = EntryKind::kPruned;
        hidden.hash = ait::internal_hash(cover.header, ait::payload_hash(cover.payload), cover.left_hash,
                                         cover.right_hash);
        p.entries[ci] = hidden;
        check(rejected(root, lo, hi, p), "omitir filas: podar un subarbol que esta dentro del rango");

        p = honest.proof;
        p.entries.erase(p.entries.begin() + c);
        check(rejected(root, lo, hi, p), "quitar la entrada de un nodo de la prueba");

        p = honest.proof;
        p.entries.push_back(p.entries.back());
        check(rejected(root, lo, hi, p), "agregar entradas de sobra");

        // Nodo dentro del rango presentado como expandido, con sus hijos como cobertura.
        p = honest.proof;
        {
            const ait::Node* node = nullptr;
            for (const ait::Node& n : tree.nodes()) {
                if (n.hash == hidden.hash) node = &n;
            }
            ait::ProofEntry expanded;
            expanded.kind = EntryKind::kExpanded;
            expanded.header = node->header;
            expanded.payload_digest = node->payload_digest;
            std::vector<ait::ProofEntry> children;
            for (std::int32_t child : {node->left, node->right}) {
                const ait::Node& n = tree.nodes()[static_cast<std::size_t>(child)];
                ait::ProofEntry e;
                e.kind = EntryKind::kCover;
                e.header = n.header;
                e.payload = n.payload;
                if (!n.header.is_leaf) {
                    e.left_hash = tree.nodes()[static_cast<std::size_t>(n.left)].hash;
                    e.right_hash = tree.nodes()[static_cast<std::size_t>(n.right)].hash;
                }
                children.push_back(e);
            }
            p.entries[ci] = expanded;
            p.entries.insert(p.entries.begin() + c + 1, children.begin(), children.end());
        }
        check(rejected(root, lo, hi, p), "presentar un nodo del rango como expandido (forma no canonica)");

        // Mentir en el encabezado de un nodo expandido para justificar una poda.
        p = honest.proof;
        for (ait::ProofEntry& e : p.entries) {
            if (e.kind == EntryKind::kExpanded && !e.header.is_leaf) {
                e.header.split_key = hi;  // haria "justificable" podar el hijo derecho
                break;
            }
        }
        check(rejected(root, lo, hi, p), "alterar las claves de un nodo expandido para justificar una poda");

        const Key wider_hi = 110;
        check(!brute_force(rows, hi + 1, wider_hi).empty() && rejected(root, lo, wider_hi, honest.proof),
              "usar la prueba de [20, 90] como si fuera de [20, 110]");

        // Digest viejo o nuevo (RNF08): la base cambio con una fila nueva dentro del rango.
        std::vector<Entry> updated = rows;
        updated.push_back({50, 201});
        const ait::Tree new_tree = ait::setup(updated, sk);
        check(rejected(new_tree.root_digest(), lo, hi, honest.proof),
              "una prueba del estado anterior no se acepta con el digest vigente");
        check(rejected(root, lo, hi, new_tree.range(lo, hi).proof),
              "una prueba del estado nuevo no se acepta con un digest viejo");

        // Prueba con miles de nodos anidados: se rechaza sin agotar la pila.
        ait::RangeProof deep;
        ait::ProofEntry nested;
        nested.kind = EntryKind::kExpanded;
        nested.header = {false, 0, 1000, 25, 1000};
        deep.entries.assign(5000, nested);
        const ait::RangeVerification dv = ait::verify_range(root, 20, 30, deep);
        check(!dv.accepted && dv.reason.find("profunda") != std::string::npos,
              "una prueba con 5000 niveles anidados se rechaza por profundidad");
    }

    // -----------------------------------------------------------------------
    section("El hueco del IntegriDB original y como lo cierra el AIT");
    {
        // Resultado honesto de [20, 90] como identificadores de fila.
        const std::vector<aso::Element> honest_ids(honest.values.begin(), honest.values.end());
        // Un servidor perezoso devuelve solo la mitad.
        const std::vector<aso::Element> partial(honest_ids.begin(), honest_ids.begin() + honest_ids.size() / 2);
        const std::vector<aso::Element> part_a(partial.begin(), partial.begin() + partial.size() / 2);
        const std::vector<aso::Element> part_b(partial.begin() + partial.size() / 2, partial.end());

        // "Nodos" fabricados solo con la clave publica, sin tocar el AIT.
        const std::vector<bn::Ec1> fake_g1 = {aso::accumulate_g1(part_a, pk), group.g1};
        const std::vector<bn::Ec2> fake_g2 = {group.g2, aso::accumulate_g2(part_b, pk)};
        const std::vector<bn::Ec1> fake_union = {aso::accumulate_g1(partial, pk)};
        check(original_single_d_verify(fake_g1, fake_g2, fake_union, partial, pk, group),
              "la replica de single_d_verify ACEPTA un resultado con la mitad de las filas "
              "(vulnerabilidad del codigo original)");

        // El mismo ataque contra nuestro verificador: un nodo de cobertura
        // fabricado con el acumulador del resultado parcial.
        std::vector<Value> partial_values(partial.begin(), partial.end());
        ait::ProofEntry forged;
        forged.kind = EntryKind::kCover;
        forged.header = {false, lo, hi, lo, partial_values.size()};
        forged.payload.acc_g1 = aso::accumulate_g1(inverses(partial_values), pk);
        forged.payload.acc_g2 = aso::accumulate_g2(inverses(partial_values), pk);
        forged.payload.enc = crypto::random_bytes(64);
        ait::RangeProof fabricated;
        fabricated.entries.push_back(forged);
        const ait::RangeVerification v = ait::verify_range(root, lo, hi, fabricated);
        check(!v.accepted && v.cover.empty(),
              "nuestro verify_range RECHAZA el mismo ataque: los nodos no pertenecen al AIT de delta");
    }

    return test::finish();
}

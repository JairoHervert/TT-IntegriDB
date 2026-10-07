// state_io_test.cpp
//
// Pruebas de los archivos que produce Setup: el digest delta (texto) y la
// base autenticada D~ (binario). Ida y vuelta sin perdidas, y rechazo de
// cualquier alteracion o de un D~ que no corresponde al delta vigente.

#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "case_study_fixture.hpp"
#include "test_support.hpp"
#include "tt/ait/range_proof.hpp"
#include "tt/ait/tree.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/io/binary.hpp"
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/setup.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace {

using namespace tt;
using scheme::AuthenticatedDbError;
using scheme::DigestFormatError;

/*
 * Parte un texto en lineas (sin el salto de linea).
 */
std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

/*
 * Une lineas en un texto, cada una con su salto de linea.
 */
std::string join(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& l : lines) text += l + "\n";
    return text;
}

/*
 * Indica si digest_from_text rechaza el texto con DigestFormatError.
 */
bool digest_rejected(const std::string& text) {
    try {
        scheme::digest_from_text(text);
        return false;
    } catch (const DigestFormatError&) {
        return true;
    }
}

/*
 * Indica si decode_authenticated_db rechaza los bytes con
 * AuthenticatedDbError.
 */
bool adb_rejected(const std::vector<unsigned char>& data, const scheme::DatabaseDigest& delta) {
    try {
        scheme::decode_authenticated_db(data, delta);
        return false;
    } catch (const AuthenticatedDbError&) {
        return true;
    }
}

/*
 * Compara dos AIT nodo por nodo: estructura, encabezados, acumuladores,
 * cifrados, valores y hashes.
 */
bool same_tree(const ait::Tree& a, const ait::Tree& b) {
    if (a.root() != b.root() || a.nodes().size() != b.nodes().size() || a.root_digest() != b.root_digest()) return false;
    for (std::size_t i = 0; i < a.nodes().size(); ++i) {
        const ait::Node& x = a.nodes()[i];
        const ait::Node& y = b.nodes()[i];
        if (!(x.header == y.header) || x.left != y.left || x.right != y.right || !(x.payload.acc_g1 == y.payload.acc_g1) ||
            !(x.payload.acc_g2 == y.payload.acc_g2) || x.payload.enc != y.payload.enc || x.values != y.values ||
            x.hash != y.hash) {
            return false;
        }
    }
    return true;
}

/*
 * Arma un AIT pequeno para las pruebas de restore.
 */
ait::Tree small_tree(const scheme::SecretKey& sk) {
    return ait::setup({{1, 10}, {2, 20}, {2, 21}, {3, 30}, {5, 50}}, sk);
}

/*
 * Intenta reconstruir un AIT y devuelve el mensaje de error, o "" si
 * ait::restore acepto los nodos.
 */
std::string restore_error(std::vector<ait::Node> nodes, std::int32_t root) {
    try {
        ait::restore(std::move(nodes), root);
        return "";
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
}

/*
 * Indica si ait::restore rechaza los nodos con std::invalid_argument.
 */
bool restore_rejected(std::vector<ait::Node> nodes, std::int32_t root) {
    return !restore_error(std::move(nodes), root).empty();
}

}  // namespace

/*
 * Pruebas de delta y D~: ida y vuelta, deteccion de cualquier linea
 * alterada de delta, deteccion de bytes alterados de D~ (con y sin suma de
 * verificacion valida), rechazo de un D~ de otro estado y reconstruccion
 * segura de un AIT a partir de nodos.
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
    std::vector<scheme::TreeSpec> specs;
    for (const auto& p : scheme::plan_trees(scheme::Preset::kQueries, test::case_study_schemas())) specs.push_back(p.spec);
    const scheme::SetupResult r = scheme::setup(base, specs, "queries", keys.sk, keys.pk);
    const scheme::DatabaseDigest& delta = r.digest;

    section("Digest delta en texto");
    {
        const std::string text = scheme::digest_to_text(delta);
        const scheme::DatabaseDigest back = scheme::digest_from_text(text);
        check(back == delta && back.id() == delta.id(), "escribir y leer delta no cambia nada");
        check(text.find("id " + crypto::to_hex(delta.id())) != std::string::npos, "el archivo muestra el id del estado");

        std::string crlf;
        for (char c : text) crlf += (c == '\n') ? std::string("\r\n") : std::string(1, c);
        check(scheme::digest_from_text(crlf) == delta, "acepta fin de linea de Windows");

        const std::vector<std::string> lines = lines_of(text);
        bool every_line = true;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::vector<std::string> edited = lines;
            char& last = edited[i].back();
            last = (last == '0') ? '1' : '0';
            every_line = every_line && digest_rejected(join(edited));
        }
        check(every_line, "cambiar el ultimo caracter de cualquiera de las " + std::to_string(lines.size()) +
                              " lineas se rechaza");

        bool removed = true;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::vector<std::string> edited = lines;
            edited.erase(edited.begin() + static_cast<std::ptrdiff_t>(i));
            removed = removed && digest_rejected(join(edited));
        }
        check(removed, "quitar cualquier linea se rechaza");
        check(digest_rejected(text + "tree extra\n"), "contenido despues de 'end' se rechaza");
        std::string spaced = text;
        spaced.replace(spaced.find("plan queries"), 12, "plan  queries");
        check(digest_rejected(spaced), "espacios dobles se rechazan (una sola forma valida)");

        scheme::DatabaseDigest unordered = delta;
        std::swap(unordered.trees[0], unordered.trees[1]);
        check_throws<DigestFormatError>([&] { scheme::validate_digest(unordered); },
                                        "arboles fuera del orden canonico se rechazan (un solo id por estado)");
        unordered = delta;
        std::swap(unordered.tables[0], unordered.tables[1]);
        check_throws<DigestFormatError>([&] { scheme::digest_to_text(unordered); },
                                        "tablas fuera de orden no se pueden escribir");
        unordered = delta;
        unordered.trees[0].value_count += 1;
        check_throws<DigestFormatError>([&] { scheme::validate_digest(unordered); },
                                        "un arbol con distinto numero de valores que filas se rechaza");
    }

    section("Base autenticada D~ en binario");
    {
        const std::vector<unsigned char> data = scheme::encode_authenticated_db(r.database, delta);
        const scheme::AuthenticatedDatabase back = scheme::decode_authenticated_db(data, delta);
        bool trees_same = back.trees.size() == r.database.trees.size();
        for (std::size_t i = 0; trees_same && i < back.trees.size(); ++i) {
            trees_same = back.trees[i].spec == r.database.trees[i].spec &&
                         same_tree(back.trees[i].tree, r.database.trees[i].tree);
        }
        bool tables_same = back.tables.size() == r.database.tables.size();
        for (std::size_t i = 0; tables_same && i < back.tables.size(); ++i) {
            tables_same = back.tables[i].schema == r.database.tables[i].schema &&
                          back.tables[i].references == r.database.tables[i].references &&
                          back.tables[i].rows.leaves() == r.database.tables[i].rows.leaves();
        }
        check(trees_same, "los AIT se reconstruyen identicos (nodos, acumuladores, cifrados y valores)");
        check(tables_same, "las tablas se reconstruyen identicas (referencias y hashes de fila)");

        const scheme::TreeSpec by_term{"academic_record", "term_code", "record_id"};
        const ait::RangeAnswer answer = back.find_tree(by_term)->range(20242, 20251);
        check(answer.values == r.database.find_tree(by_term)->range(20242, 20251).values &&
                  ait::verify_range(delta.find_tree(by_term)->root, 20242, 20251, answer.proof).accepted,
              "el D~ leido responde y sus pruebas se verifican contra delta");

        bool flips = true;
        std::size_t tried = 0;
        for (std::size_t pos = 0; pos < data.size(); pos += 97) {
            std::vector<unsigned char> bad = data;
            bad[pos] ^= 0x40;
            flips = flips && adb_rejected(bad, delta);
            ++tried;
        }
        check(flips, "alterar cualquier byte se detecta (" + std::to_string(tried) + " posiciones probadas)");
        check(adb_rejected(std::vector<unsigned char>(data.begin(), data.end() - 1), delta) &&
                  adb_rejected(std::vector<unsigned char>(data.begin(), data.begin() + 20), delta),
              "un archivo truncado se rechaza");

        scheme::AuthenticatedDatabase forged = r.database;
        std::vector<ait::Node> nodes = forged.trees[0].tree.nodes();
        const std::int32_t root = forged.trees[0].tree.root();
        nodes[static_cast<std::size_t>(root)].payload.acc_g1 =
            nodes[static_cast<std::size_t>(root)].payload.acc_g1 + crypto::init_pairing_group().g1;
        forged.trees[0].tree = ait::restore(std::move(nodes), root);
        check(adb_rejected(scheme::encode_authenticated_db(forged, delta), delta),
              "un acumulador cambiado se detecta aunque la suma de verificacion sea valida");

        db::Database changed = base;
        std::int64_t& grade = std::get<std::int64_t>(changed.tables[0].rows[3][5]);
        grade = grade == 10 ? 9 : 10;
        const scheme::SetupResult other = scheme::setup(changed, specs, "queries", keys.sk, keys.pk);
        check(adb_rejected(scheme::encode_authenticated_db(other.database, other.digest), delta),
              "un D~ de otro estado de la base se rechaza con el delta vigente");
    }

    section("Archivos");
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / ("tt_state_io_test_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        scheme::save_digest(dir / "delta.txt", delta);
        scheme::save_authenticated_db(dir / "authenticated_db.bin", r.database, delta);
        const scheme::DatabaseDigest loaded = scheme::load_digest(dir / "delta.txt");
        const scheme::AuthenticatedDatabase adb = scheme::load_authenticated_db(dir / "authenticated_db.bin", loaded);
        check(loaded == delta && adb.trees.size() == 11, "guardar y cargar delta y D~");
        std::size_t files = 0;
        bool no_temporaries = true;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            ++files;
            no_temporaries = no_temporaries && entry.path().filename().string().find(".tmp") == std::string::npos;
        }
        check(files == 2 && no_temporaries, "quedan solo los dos archivos, sin temporales");
        check_throws<std::runtime_error>([&] { scheme::load_digest(dir / "no_existe.txt"); },
                                         "cargar un delta que no existe lanza un error");
        std::filesystem::remove_all(dir);
    }

    section("Reconstruccion de un AIT a partir de nodos");
    {
        const ait::Tree tree = small_tree(keys.sk);
        const std::vector<ait::Node>& good = tree.nodes();
        const std::int32_t root = tree.root();
        check(same_tree(ait::restore(good, root), tree), "nodos correctos dan el mismo arbol y la misma raiz");

        std::size_t leaf = 0, internal = 0;
        while (!good[leaf].header.is_leaf) ++leaf;
        while (good[internal].header.is_leaf) ++internal;

        std::vector<ait::Node> n = good;
        n[internal].right = n[internal].left;
        check(restore_rejected(n, root), "un nodo con el mismo hijo dos veces se rechaza");
        n = good;
        n[static_cast<std::size_t>(root)].left = static_cast<std::int32_t>(leaf);
        check(restore_rejected(n, root), "un nodo con dos padres (y otro suelto) se rechaza");
        n = good;
        const ait::Node& top = good[static_cast<std::size_t>(root)];
        n[static_cast<std::size_t>(top.right)].right = good[static_cast<std::size_t>(top.left)].left;
        check(restore_error(n, root).find("mas de un padre") != std::string::npos,
              "un nodo compartido por dos ramas se detecta antes de recorrer (evita recorridos exponenciales)");
        n = good;
        n[leaf].values.push_back(99);
        check(restore_rejected(n, root), "una hoja con mas valores que su contador se rechaza");
        n = good;
        n[leaf].values[0] = 0;
        check(restore_rejected(n, root), "una hoja con valor 0 se rechaza");
        n = good;
        n[static_cast<std::size_t>(root)].header.split_key += 1;
        check(restore_rejected(n, root), "un nodo interno con clave que no coincide con sus hijos se rechaza");
        n = good;
        n[static_cast<std::size_t>(root)].header.count += 1;
        check(restore_rejected(n, root), "un contador que no es la suma de los hijos se rechaza");
        check(restore_rejected(good, static_cast<std::int32_t>(leaf)), "una raiz que no es la raiz se rechaza");
        check(restore_rejected(good, static_cast<std::int32_t>(good.size())), "una raiz fuera de rango se rechaza");
        check(ait::restore({}, -1).empty() && restore_rejected({}, 0), "sin nodos solo vale la raiz -1");
    }

    return test::finish();
}

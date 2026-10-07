// authenticated_db.cpp

#include "tt/scheme/authenticated_db.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>

#include "tt/io/binary.hpp"
#include "tt/io/file.hpp"

namespace tt::scheme {

namespace {

constexpr unsigned char kMagic[8] = {'T', 'T', 'A', 'D', 'B', 0, 0, 0};
constexpr std::uint32_t kFormatVersion = 1;

// Limites de lectura: ningun archivo valido los alcanza, y evitan reservar
// memoria absurda si un campo esta alterado.
constexpr std::size_t kMaxName = 64;
constexpr std::size_t kMaxColumns = 4096;
constexpr std::size_t kMaxCipher = 1024;

// Tamano minimo en el archivo de una fila y de un nodo (para require_items).
constexpr std::size_t kRowBytes = 8 + crypto::kDigestBytes;
constexpr std::size_t kNodeBytes = 1 + 4 * 8 + 2 * 4 + crypto::kG1EncodedBytes + crypto::kG2EncodedBytes + 4 + 8;

/*
 * Escribe un AIT: su identificacion, la raiz y cada nodo con encabezado,
 * hijos, acumuladores, cifrado y (en las hojas) los valores.
 */
void write_tree(io::BinaryWriter& w, const AuthenticatedTree& t) {
    w.text(t.spec.table).text(t.spec.key_column).text(t.spec.value_column);
    const std::vector<ait::Node>& nodes = t.tree.nodes();
    w.i32(t.tree.root()).u64(nodes.size());
    for (const ait::Node& n : nodes) {
        const ait::NodeHeader& h = n.header;
        w.u8(h.is_leaf ? 1 : 0).i64(h.min_key).i64(h.max_key).i64(h.split_key).u64(h.count);
        w.i32(n.left).i32(n.right);
        w.point(n.payload.acc_g1).point(n.payload.acc_g2).bytes(n.payload.enc);
        w.u64(n.values.size());
        for (ait::Value v : n.values) w.i64(v);
    }
}

/*
 * Lee un AIT escrito por write_tree y lo reconstruye con ait::restore.
 *
 * Errores:
 *   Lanza AuthenticatedDbError si el formato o la estructura no son validos.
 */
AuthenticatedTree read_tree(io::BinaryReader& r) {
    AuthenticatedTree t;
    t.spec.table = r.text(kMaxName);
    t.spec.key_column = r.text(kMaxName);
    t.spec.value_column = r.text(kMaxName);
    const std::int32_t root = r.i32();
    const std::uint64_t count = r.u64();
    r.require_items(count, kNodeBytes);

    std::vector<ait::Node> nodes(static_cast<std::size_t>(count));
    for (ait::Node& n : nodes) {
        const std::uint8_t leaf = r.u8();
        if (leaf > 1) throw AuthenticatedDbError("marca de hoja invalida en " + t.spec.label());
        n.header.is_leaf = leaf == 1;
        n.header.min_key = r.i64();
        n.header.max_key = r.i64();
        n.header.split_key = r.i64();
        n.header.count = r.u64();
        n.left = r.i32();
        n.right = r.i32();
        n.payload.acc_g1 = r.g1();
        n.payload.acc_g2 = r.g2();
        n.payload.enc = r.bytes(kMaxCipher);
        const std::uint64_t values = r.u64();
        r.require_items(values, 8);
        n.values.resize(static_cast<std::size_t>(values));
        for (ait::Value& v : n.values) v = r.i64();
    }
    try {
        t.tree = ait::restore(std::move(nodes), root);
    } catch (const std::invalid_argument& e) {
        throw AuthenticatedDbError(t.spec.label() + ": " + e.what());
    }
    return t;
}

/*
 * Escribe la parte de una tabla: estructura y, por cada fila, su valor de
 * referencia y su hash (la hoja del arbol de Merkle).
 */
void write_table(io::BinaryWriter& w, const AuthenticatedTable& t) {
    w.text(t.schema.name).u32(static_cast<std::uint32_t>(t.schema.columns.size()));
    for (const db::Column& c : t.schema.columns) w.text(c.name).u8(static_cast<std::uint8_t>(c.type));
    w.u32(static_cast<std::uint32_t>(t.schema.reference_column));
    const std::vector<crypto::Digest>& leaves = t.rows.leaves();
    w.u64(leaves.size());
    for (std::size_t i = 0; i < leaves.size(); ++i) w.i64(t.references[i]).digest(leaves[i]);
}

/*
 * Lee una tabla escrita por write_table y reconstruye su arbol de Merkle.
 *
 * Errores:
 *   Lanza AuthenticatedDbError si el formato no es valido o si los valores
 *   de referencia no van en orden estrictamente ascendente.
 */
AuthenticatedTable read_table(io::BinaryReader& r) {
    AuthenticatedTable t;
    t.schema.name = r.text(kMaxName);
    const std::uint32_t columns = r.u32();
    if (columns > kMaxColumns) throw AuthenticatedDbError("tabla " + t.schema.name + " con demasiadas columnas");
    for (std::uint32_t i = 0; i < columns; ++i) {
        db::Column c;
        c.name = r.text(kMaxName);
        const std::uint8_t type = r.u8();
        if (type > 1) throw AuthenticatedDbError("tipo de columna invalido en " + t.schema.name);
        c.type = static_cast<db::ColumnType>(type);
        t.schema.columns.push_back(c);
    }
    t.schema.reference_column = r.u32();
    const std::uint64_t rows = r.u64();
    r.require_items(rows, kRowBytes);
    std::vector<crypto::Digest> leaves(static_cast<std::size_t>(rows));
    t.references.resize(static_cast<std::size_t>(rows));
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        t.references[i] = r.i64();
        leaves[i] = r.digest();
        if (i > 0 && !(t.references[i - 1] < t.references[i])) {
            throw AuthenticatedDbError("tabla " + t.schema.name + ": referencias fuera de orden o repetidas");
        }
    }
    t.rows = merkle::Tree(std::move(leaves));
    return t;
}

/*
 * SHA-256 simple de un bloque de bytes (la suma de verificacion del
 * archivo).
 */
crypto::Digest checksum(const unsigned char* data, std::size_t size) {
    crypto::Sha256 h;
    h.bytes(data, size);
    return h.finish();
}

/*
 * Compara lo leido del archivo con lo que dice el digest: mismas tablas
 * (estructura, filas y raiz de Merkle) y mismos arboles (identificacion,
 * contadores y raiz), en el mismo orden.
 *
 * Errores:
 *   Lanza AuthenticatedDbError con la primera diferencia encontrada.
 */
void check_against_digest(const AuthenticatedDatabase& d, const DatabaseDigest& digest) {
    if (d.tables.size() != digest.tables.size() || d.trees.size() != digest.trees.size()) {
        throw AuthenticatedDbError("D~ y delta no tienen las mismas tablas y arboles");
    }
    for (std::size_t i = 0; i < d.tables.size(); ++i) {
        const AuthenticatedTable& t = d.tables[i];
        const TableDigest& e = digest.tables[i];
        if (!(t.schema == e.schema) || t.rows.size() != e.row_count || t.rows.root() != e.rows_root) {
            throw AuthenticatedDbError("la tabla " + t.schema.name + " no coincide con delta");
        }
    }
    for (std::size_t i = 0; i < d.trees.size(); ++i) {
        const AuthenticatedTree& t = d.trees[i];
        const TreeDigest& e = digest.trees[i];
        if (!(t.spec == e.spec) || t.tree.leaf_count() != e.leaf_count || t.tree.value_count() != e.value_count ||
            t.tree.root_digest() != e.root) {
            throw AuthenticatedDbError("el arbol " + t.spec.label() + " no coincide con delta");
        }
    }
}

}  // namespace

/*
 * Implementacion: busqueda binaria sobre los valores de referencia, que
 * estan ordenados.
 */
std::optional<std::uint64_t> AuthenticatedTable::position_of(std::int64_t reference) const {
    const auto it = std::lower_bound(references.begin(), references.end(), reference);
    if (it == references.end() || *it != reference) return std::nullopt;
    return static_cast<std::uint64_t>(it - references.begin());
}

/*
 * Implementacion: busqueda lineal por nombre.
 */
const AuthenticatedTable* AuthenticatedDatabase::find_table(const std::string& table) const {
    for (const AuthenticatedTable& t : tables) {
        if (t.schema.name == table) return &t;
    }
    return nullptr;
}

/*
 * Implementacion: busqueda lineal.
 */
const ait::Tree* AuthenticatedDatabase::find_tree(const TreeSpec& spec) const {
    for (const AuthenticatedTree& t : trees) {
        if (t.spec == spec) return &t.tree;
    }
    return nullptr;
}

/*
 * Implementacion: encabezado, tablas y arboles con BinaryWriter; al final
 * agrega el SHA-256 de todo lo escrito.
 */
std::vector<unsigned char> encode_authenticated_db(const AuthenticatedDatabase& database,
                                                   const DatabaseDigest& digest) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u32(kFormatVersion).digest(digest.id());
    w.u32(static_cast<std::uint32_t>(database.tables.size()));
    for (const AuthenticatedTable& t : database.tables) write_table(w, t);
    w.u32(static_cast<std::uint32_t>(database.trees.size()));
    for (const AuthenticatedTree& t : database.trees) write_tree(w, t);

    std::vector<unsigned char> data = w.data();
    const crypto::Digest sum = checksum(data.data(), data.size());
    data.insert(data.end(), sum.begin(), sum.end());
    return data;
}

/*
 * Implementacion: primero la suma de verificacion (un archivo danado se
 * rechaza antes de interpretarlo), despues el encabezado y el id, las
 * tablas, los arboles y, al final, la comparacion con el digest.
 */
AuthenticatedDatabase decode_authenticated_db(const std::vector<unsigned char>& data,
                                              const DatabaseDigest& digest) {
    if (data.size() < sizeof kMagic + 4 + 2 * crypto::kDigestBytes) {
        throw AuthenticatedDbError("el archivo es demasiado corto para ser un D~");
    }
    const std::size_t body = data.size() - crypto::kDigestBytes;
    const crypto::Digest sum = checksum(data.data(), body);
    if (!std::equal(sum.begin(), sum.end(), data.begin() + static_cast<std::ptrdiff_t>(body))) {
        throw AuthenticatedDbError("la suma de verificacion no coincide: el archivo esta danado o incompleto");
    }

    AuthenticatedDatabase d;
    try {
        io::BinaryReader r(data.data(), body);
        if (!std::equal(std::begin(kMagic), std::end(kMagic), r.raw(sizeof kMagic))) {
            throw AuthenticatedDbError("el archivo no es un D~ de tt-integridb");
        }
        if (r.u32() != kFormatVersion) throw AuthenticatedDbError("version de formato de D~ desconocida");
        if (r.digest() != digest.id()) {
            throw AuthenticatedDbError("este D~ es de otro estado de la base: su id no es el del delta vigente");
        }
        const std::uint32_t tables = r.u32();
        r.require_items(tables, 4 + 4 + 4 + 8);
        for (std::uint32_t i = 0; i < tables; ++i) d.tables.push_back(read_table(r));
        const std::uint32_t trees = r.u32();
        r.require_items(trees, 3 * 4 + 4 + 8);
        for (std::uint32_t i = 0; i < trees; ++i) d.trees.push_back(read_tree(r));
        r.expect_end();
    } catch (const io::FormatError& e) {
        throw AuthenticatedDbError(std::string("formato invalido: ") + e.what());
    }
    check_against_digest(d, digest);
    return d;
}

/*
 * Implementacion: codifica y escribe de forma atomica.
 */
void save_authenticated_db(const std::filesystem::path& path, const AuthenticatedDatabase& database,
                           const DatabaseDigest& digest) {
    io::write_file_atomic(path, encode_authenticated_db(database, digest));
}

/*
 * Implementacion: lee el archivo y lo decodifica; agrega la ruta a los
 * mensajes de error.
 */
AuthenticatedDatabase load_authenticated_db(const std::filesystem::path& path, const DatabaseDigest& digest) {
    const std::vector<unsigned char> data = io::read_file(path);
    try {
        return decode_authenticated_db(data, digest);
    } catch (const AuthenticatedDbError& e) {
        throw AuthenticatedDbError(path.string() + ": " + e.what());
    }
}

}  // namespace tt::scheme

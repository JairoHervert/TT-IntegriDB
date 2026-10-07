// digest.cpp

#include "tt/scheme/digest.hpp"

#include <sstream>
#include <string>

#include "tt/io/file.hpp"

namespace tt::scheme {

namespace {

constexpr std::uint8_t kTagDigest = 0x20;
constexpr std::uint32_t kFormatVersion = 1;
const char* const kHeader = "tt-integridb digest v1";

/*
 * Separa una linea en palabras separadas por un solo espacio. Devuelve una
 * lista vacia si la linea tiene espacios al inicio o al final, dos espacios
 * seguidos o tabuladores: el formato no admite variantes.
 */
std::vector<std::string> split_strict(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : line) {
        if (c == ' ') {
            if (current.empty()) return {};
            tokens.push_back(current);
            current.clear();
        } else if (c == '\t') {
            return {};
        } else {
            current.push_back(c);
        }
    }
    if (current.empty()) return {};
    tokens.push_back(current);
    return tokens;
}

/*
 * Convierte un entero decimal canonico (sin signo, sin ceros a la
 * izquierda, a lo mas 20 digitos) en u64.
 *
 * Devuelve:
 *   true y el valor en `out` si el texto es valido y cabe en 64 bits.
 */
bool parse_u64(const std::string& text, std::uint64_t& out) {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0')) return false;
    std::uint64_t v = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const std::uint64_t d = static_cast<std::uint64_t>(c - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

/*
 * Recorre el texto linea por linea llevando la cuenta del numero de linea,
 * para que cada error diga donde esta el problema.
 */
class Lines {
public:
    /*
     * Parte el texto en lineas; acepta el fin de linea de Windows.
     */
    explicit Lines(const std::string& text) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines_.push_back(line);
        }
    }

    /*
     * Devuelve la siguiente linea sin consumirla, o "" si ya no hay.
     */
    const std::string& peek() const {
        static const std::string kEmpty;
        return pos_ < lines_.size() ? lines_[pos_] : kEmpty;
    }

    /*
     * Devuelve la siguiente linea separada en palabras y la consume.
     *
     * Errores:
     *   Lanza DigestFormatError si ya no hay lineas o si la linea no respeta
     *   el formato de espacios.
     */
    std::vector<std::string> next(const std::string& what) {
        if (pos_ >= lines_.size()) fail_at(lines_.size(), "el archivo termina antes de tiempo; se esperaba " + what);
        ++pos_;
        std::vector<std::string> tokens = split_strict(lines_[pos_ - 1]);
        if (tokens.empty()) fail("linea mal formada; se esperaba " + what);
        return tokens;
    }

    /*
     * Exige que las lineas restantes esten vacias.
     */
    void expect_end_of_input() {
        for (; pos_ < lines_.size(); ++pos_) {
            if (!lines_[pos_].empty()) fail_at(pos_ + 1, "hay contenido despues de 'end'");
        }
    }

    /*
     * Lanza DigestFormatError con el numero de la ultima linea leida.
     */
    [[noreturn]] void fail(const std::string& message) const { fail_at(pos_, message); }

private:
    /*
     * Lanza DigestFormatError con el numero de linea indicado.
     */
    [[noreturn]] static void fail_at(std::size_t line, const std::string& message) {
        throw DigestFormatError("delta, linea " + std::to_string(line) + ": " + message);
    }

    std::vector<std::string> lines_;
    std::size_t pos_ = 0;
};

/*
 * Lee una linea "<nombre> <entero>" y devuelve el entero.
 */
std::uint64_t expect_number_line(Lines& lines, const std::string& name) {
    const std::vector<std::string> t = lines.next("'" + name + " <entero>'");
    std::uint64_t v = 0;
    if (t.size() != 2 || t[0] != name || !parse_u64(t[1], v)) lines.fail("se esperaba '" + name + " <entero>'");
    return v;
}

/*
 * Lee un digest hexadecimal de un token, o falla con un mensaje claro.
 */
crypto::Digest expect_hex(Lines& lines, const std::string& token, const std::string& what) {
    crypto::Digest d{};
    if (!parse_hex_digest(token, d)) lines.fail(what + ": se esperaban 64 digitos hexadecimales en minusculas");
    return d;
}

/*
 * Lee un numero de un token, o falla con un mensaje claro.
 */
std::uint64_t expect_u64(Lines& lines, const std::string& token, const std::string& what) {
    std::uint64_t v = 0;
    if (!parse_u64(token, v)) lines.fail(what + ": se esperaba un entero decimal");
    return v;
}

/*
 * Lee una linea "table ..." y las lineas "column ..." que la siguen.
 */
TableDigest parse_table(Lines& lines) {
    const std::vector<std::string> t = lines.next("una tabla");
    if (t.size() != 8 || t[0] != "table" || t[2] != "reference" || t[4] != "rows" || t[6] != "root") {
        lines.fail("se esperaba 'table <nombre> reference <columna> rows <n> root <hex>'");
    }
    TableDigest table;
    table.schema.name = t[1];
    table.row_count = expect_u64(lines, t[5], "rows");
    table.rows_root = expect_hex(lines, t[7], "root");

    while (lines.peek().rfind("column ", 0) == 0) {
        const std::vector<std::string> c = lines.next("una columna");
        if (c.size() != 4 || c[0] != "column") lines.fail("se esperaba 'column <tabla> <columna> <tipo>'");
        if (c[1] != table.schema.name) lines.fail("la columna " + c[2] + " no es de la tabla " + table.schema.name);
        const std::optional<db::ColumnType> type = db::parse_column_type(c[3]);
        if (!type) lines.fail("tipo de columna desconocido '" + c[3] + "'");
        table.schema.columns.push_back(db::Column{c[2], *type});
    }
    const std::optional<std::size_t> ref = table.schema.column_index(t[3]);
    if (!ref) lines.fail("la tabla " + t[1] + " no tiene la columna de referencia " + t[3]);
    table.schema.reference_column = *ref;
    return table;
}

/*
 * Lee una linea "tree ...".
 */
TreeDigest parse_tree(Lines& lines) {
    const std::vector<std::string> t = lines.next("un arbol");
    if (t.size() != 10 || t[0] != "tree" || t[4] != "leaves" || t[6] != "values" || t[8] != "root") {
        lines.fail("se esperaba 'tree <tabla> <clave> <valor> leaves <n> values <n> root <hex>'");
    }
    TreeDigest tree;
    tree.spec = TreeSpec{t[1], t[2], t[3]};
    tree.leaf_count = expect_u64(lines, t[5], "leaves");
    tree.value_count = expect_u64(lines, t[7], "values");
    tree.root = expect_hex(lines, t[9], "root");
    return tree;
}

/*
 * Agrega un texto al hash con su longitud por delante.
 */
void hash_text(crypto::Sha256& h, const std::string& s) { h.u64(s.size()).bytes(s.data(), s.size()); }

}  // namespace

/*
 * Implementacion: etiqueta 0x20, version del formato y cada campo en orden,
 * con las listas precedidas por su tamano y los textos por su longitud.
 */
crypto::Digest DatabaseDigest::id() const {
    crypto::Sha256 h;
    h.u8(kTagDigest).u64(kFormatVersion).u64(state_version).digest(key_fingerprint);
    hash_text(h, plan);
    h.u64(tables.size());
    for (const TableDigest& t : tables) {
        hash_text(h, t.schema.name);
        h.u64(t.schema.columns.size());
        for (const db::Column& c : t.schema.columns) {
            hash_text(h, c.name);
            h.u8(static_cast<std::uint8_t>(c.type));
        }
        h.u64(t.schema.reference_column).u64(t.row_count).digest(t.rows_root);
    }
    h.u64(trees.size());
    for (const TreeDigest& t : trees) {
        hash_text(h, t.spec.table);
        hash_text(h, t.spec.key_column);
        hash_text(h, t.spec.value_column);
        h.u64(t.leaf_count).u64(t.value_count).digest(t.root);
    }
    return h.finish();
}

/*
 * Implementacion: busqueda lineal por nombre.
 */
const TableDigest* DatabaseDigest::find_table(const std::string& table) const {
    for (const TableDigest& t : tables) {
        if (t.schema.name == table) return &t;
    }
    return nullptr;
}

/*
 * Implementacion: busqueda lineal.
 */
const TreeDigest* DatabaseDigest::find_tree(const TreeSpec& spec) const {
    for (const TreeDigest& t : trees) {
        if (t.spec == spec) return &t;
    }
    return nullptr;
}

/*
 * Implementacion: revisa los campos generales, luego las tablas (con
 * db::validate_schema y el orden por nombre) y al final los arboles
 * (validate_plan hace las revisiones de tabla y columnas).
 */
void validate_digest(const DatabaseDigest& digest) {
    if (digest.state_version == 0) throw DigestFormatError("delta: state_version debe ser al menos 1");
    if (!db::is_valid_identifier(digest.plan)) throw DigestFormatError("delta: nombre de plan invalido");

    std::vector<db::TableSchema> schemas;
    for (std::size_t i = 0; i < digest.tables.size(); ++i) {
        const TableDigest& t = digest.tables[i];
        try {
            db::validate_schema(t.schema);
        } catch (const db::DataError& e) {
            throw DigestFormatError(std::string("delta: ") + e.what());
        }
        if (i > 0 && !(digest.tables[i - 1].schema.name < t.schema.name)) {
            throw DigestFormatError("delta: las tablas deben ir ordenadas por nombre y sin repetir");
        }
        schemas.push_back(t.schema);
    }

    std::vector<TreeSpec> specs;
    for (std::size_t i = 0; i < digest.trees.size(); ++i) {
        const TreeDigest& t = digest.trees[i];
        if (i > 0 && !(digest.trees[i - 1].spec < t.spec)) {
            throw DigestFormatError("delta: los arboles deben ir en orden canonico y sin repetir");
        }
        const TableDigest* table = digest.find_table(t.spec.table);
        if (table != nullptr && t.value_count != table->row_count) {
            throw DigestFormatError("delta: el arbol " + t.spec.label() + " no tiene un valor por cada fila de " +
                                    t.spec.table);
        }
        if (t.leaf_count > t.value_count || (t.value_count > 0 && t.leaf_count == 0)) {
            throw DigestFormatError("delta: el arbol " + t.spec.label() + " tiene un numero de hojas imposible");
        }
        specs.push_back(t.spec);
    }
    try {
        validate_plan(specs, schemas);
    } catch (const PlanError& e) {
        throw DigestFormatError(std::string("delta: ") + e.what());
    }
}

/*
 * Implementacion: valida y escribe linea por linea en el orden del formato.
 */
std::string digest_to_text(const DatabaseDigest& digest) {
    validate_digest(digest);
    std::ostringstream out;
    out << kHeader << '\n'
        << "state_version " << digest.state_version << '\n'
        << "key_fingerprint " << crypto::to_hex(digest.key_fingerprint) << '\n'
        << "plan " << digest.plan << '\n';
    for (const TableDigest& t : digest.tables) {
        out << "table " << t.schema.name << " reference " << t.schema.columns[t.schema.reference_column].name
            << " rows " << t.row_count << " root " << crypto::to_hex(t.rows_root) << '\n';
        for (const db::Column& c : t.schema.columns) {
            out << "column " << t.schema.name << ' ' << c.name << ' ' << db::column_type_name(c.type) << '\n';
        }
    }
    for (const TreeDigest& t : digest.trees) {
        out << "tree " << t.spec.table << ' ' << t.spec.key_column << ' ' << t.spec.value_column << " leaves "
            << t.leaf_count << " values " << t.value_count << " root " << crypto::to_hex(t.root) << '\n';
    }
    out << "id " << crypto::to_hex(digest.id()) << '\n' << "end\n";
    return out.str();
}

/*
 * Implementacion: lee el encabezado y los campos generales; despues todas
 * las lineas "table" (con sus "column") y todas las "tree"; al final "id" y
 * "end". Valida la coherencia y compara el id escrito con el recalculado.
 */
DatabaseDigest digest_from_text(const std::string& text) {
    Lines lines(text);
    const std::vector<std::string> header = lines.next("el encabezado");
    std::string joined;
    for (std::size_t i = 0; i < header.size(); ++i) joined += (i ? " " : "") + header[i];
    if (joined != kHeader) lines.fail(std::string("se esperaba '") + kHeader + "'");

    DatabaseDigest digest;
    digest.state_version = expect_number_line(lines, "state_version");
    const std::vector<std::string> fp = lines.next("'key_fingerprint <hex>'");
    if (fp.size() != 2 || fp[0] != "key_fingerprint") lines.fail("se esperaba 'key_fingerprint <hex>'");
    digest.key_fingerprint = expect_hex(lines, fp[1], "key_fingerprint");
    const std::vector<std::string> plan = lines.next("'plan <nombre>'");
    if (plan.size() != 2 || plan[0] != "plan") lines.fail("se esperaba 'plan <nombre>'");
    digest.plan = plan[1];

    while (lines.peek().rfind("table ", 0) == 0) digest.tables.push_back(parse_table(lines));
    while (lines.peek().rfind("tree ", 0) == 0) digest.trees.push_back(parse_tree(lines));

    const std::vector<std::string> id = lines.next("'id <hex>'");
    if (id.size() != 2 || id[0] != "id") lines.fail("se esperaba 'id <hex>' (o una linea table/tree fuera de lugar)");
    const crypto::Digest written = expect_hex(lines, id[1], "id");
    const std::vector<std::string> end = lines.next("'end'");
    if (end.size() != 1 || end[0] != "end") lines.fail("se esperaba 'end'");
    lines.expect_end_of_input();

    validate_digest(digest);
    if (digest.id() != written) {
        throw DigestFormatError("delta: el id no coincide con el contenido; el archivo fue modificado o esta danado");
    }
    return digest;
}

/*
 * Implementacion: convierte a texto y lo escribe de forma atomica.
 */
void save_digest(const std::filesystem::path& path, const DatabaseDigest& digest) {
    io::write_file_atomic(path, digest_to_text(digest));
}

/*
 * Implementacion: lee el archivo completo y lo interpreta; si hay error de
 * formato, agrega la ruta al mensaje.
 */
DatabaseDigest load_digest(const std::filesystem::path& path) {
    const std::vector<unsigned char> data = io::read_file(path);
    try {
        return digest_from_text(std::string(data.begin(), data.end()));
    } catch (const DigestFormatError& e) {
        throw DigestFormatError(path.string() + ": " + e.what());
    }
}

/*
 * Implementacion: exige exactamente 64 caracteres de 0-9 y a-f.
 */
bool parse_hex_digest(const std::string& hex, crypto::Digest& out) {
    if (hex.size() != 2 * out.size()) return false;

    /*
     * Valor de un digito hexadecimal en minusculas, o -1 si no lo es.
     */
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = value(hex[2 * i]);
        const int lo = value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

}  // namespace tt::scheme

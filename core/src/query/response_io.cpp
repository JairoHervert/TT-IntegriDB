// response_io.cpp

#include "tt/query/response_io.hpp"

#include <algorithm>
#include <iterator>
#include <string>

#include "tt/io/binary.hpp"
#include "tt/io/file.hpp"

namespace tt::query {

namespace {

constexpr unsigned char kMagic[8] = {'T', 'T', 'R', 'S', 'P', 0, 0, 0};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint8_t kKindSelection = 1;

// Limites de lectura: ninguna respuesta valida los alcanza y evitan
// reservar memoria absurda si un campo esta alterado.
constexpr std::size_t kMaxName = 64;
constexpr std::size_t kMaxCells = 4096;
constexpr std::size_t kMaxText = 1 << 20;
constexpr std::size_t kMaxCipher = 1024;
constexpr std::size_t kMaxPath = 64;  // un arbol de Merkle con 2^64 hojas tiene 64 niveles

// Tamano minimo de cada elemento repetido (para require_items).
constexpr std::size_t kEntryBytes = 1 + crypto::kDigestBytes;  // la entrada mas chica: podada
constexpr std::size_t kRowBytes = 4 + 8 + 4;                   // sin celdas y sin hermanos
constexpr std::size_t kCellBytes = 1 + 4;                      // texto vacio

/*
 * Escribe el encabezado de un nodo.
 */
void write_header(io::BinaryWriter& w, const ait::NodeHeader& h) {
    w.u8(h.is_leaf ? 1 : 0).i64(h.min_key).i64(h.max_key).i64(h.split_key).u64(h.count);
}

/*
 * Lee el encabezado de un nodo.
 *
 * Errores:
 *   Lanza io::FormatError si el indicador de hoja no es 0 ni 1.
 */
ait::NodeHeader read_header(io::BinaryReader& r) {
    ait::NodeHeader h;
    const std::uint8_t leaf = r.u8();
    if (leaf > 1) throw io::FormatError("indicador de hoja invalido");
    h.is_leaf = leaf == 1;
    h.min_key = r.i64();
    h.max_key = r.i64();
    h.split_key = r.i64();
    h.count = r.u64();
    return h;
}

/*
 * Escribe una entrada de la prueba del AIT segun su tipo.
 */
void write_entry(io::BinaryWriter& w, const ait::ProofEntry& e) {
    w.u8(static_cast<std::uint8_t>(e.kind));
    switch (e.kind) {
        case ait::EntryKind::kCover:
            write_header(w, e.header);
            w.point(e.payload.acc_g1).point(e.payload.acc_g2).bytes(e.payload.enc);
            if (!e.header.is_leaf) w.digest(e.left_hash).digest(e.right_hash);
            break;
        case ait::EntryKind::kExpanded:
            write_header(w, e.header);
            w.digest(e.payload_digest);
            break;
        case ait::EntryKind::kPruned:
            w.digest(e.hash);
            break;
    }
}

/*
 * Lee una entrada de la prueba del AIT.
 *
 * Errores:
 *   Lanza io::FormatError si el tipo es desconocido o un campo no es valido.
 */
ait::ProofEntry read_entry(io::BinaryReader& r) {
    ait::ProofEntry e;
    const std::uint8_t kind = r.u8();
    switch (kind) {
        case static_cast<std::uint8_t>(ait::EntryKind::kCover):
            e.kind = ait::EntryKind::kCover;
            e.header = read_header(r);
            e.payload.acc_g1 = r.g1();
            e.payload.acc_g2 = r.g2();
            e.payload.enc = r.bytes(kMaxCipher);
            if (!e.header.is_leaf) {
                e.left_hash = r.digest();
                e.right_hash = r.digest();
            }
            break;
        case static_cast<std::uint8_t>(ait::EntryKind::kExpanded):
            e.kind = ait::EntryKind::kExpanded;
            e.header = read_header(r);
            e.payload_digest = r.digest();
            break;
        case static_cast<std::uint8_t>(ait::EntryKind::kPruned):
            e.kind = ait::EntryKind::kPruned;
            e.hash = r.digest();
            break;
        default:
            throw io::FormatError("tipo de entrada de la prueba desconocido");
    }
    return e;
}

/*
 * Escribe una fila (cada celda con su tipo) y su prueba de Merkle.
 */
void write_row(io::BinaryWriter& w, const RowProof& row) {
    w.u32(static_cast<std::uint32_t>(row.row.size()));
    for (const db::Cell& cell : row.row) {
        if (const auto* v = std::get_if<std::int64_t>(&cell)) {
            w.u8(0).i64(*v);
        } else {
            w.u8(1).text(std::get<std::string>(cell));
        }
    }
    w.u64(row.inclusion.index).u32(static_cast<std::uint32_t>(row.inclusion.path.size()));
    for (const crypto::Digest& d : row.inclusion.path) w.digest(d);
}

/*
 * Lee una fila escrita por write_row.
 *
 * Errores:
 *   Lanza io::FormatError si un tipo de celda es desconocido o una longitud
 *   excede los limites.
 */
RowProof read_row(io::BinaryReader& r) {
    RowProof row;
    const std::uint32_t cells = r.u32();
    if (cells > kMaxCells) throw io::FormatError("una fila tiene demasiadas celdas");
    r.require_items(cells, kCellBytes);
    row.row.reserve(cells);
    for (std::uint32_t i = 0; i < cells; ++i) {
        const std::uint8_t type = r.u8();
        if (type == 0) {
            row.row.emplace_back(r.i64());
        } else if (type == 1) {
            row.row.emplace_back(r.text(kMaxText));
        } else {
            throw io::FormatError("tipo de celda desconocido");
        }
    }
    row.inclusion.index = r.u64();
    const std::uint32_t path = r.u32();
    if (path > kMaxPath) throw io::FormatError("una prueba de Merkle tiene demasiados hermanos");
    r.require_items(path, crypto::kDigestBytes);
    row.inclusion.path.reserve(path);
    for (std::uint32_t i = 0; i < path; ++i) row.inclusion.path.push_back(r.digest());
    return row;
}

}  // namespace

/*
 * Implementacion: encabezado, estado, consulta, entradas de la prueba y
 * filas, en el orden descrito en response_io.hpp.
 */
std::vector<unsigned char> encode_response(const SelectionResponse& response) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u32(kFormatVersion).u8(kKindSelection).digest(response.state_id);
    w.text(response.query.table).text(response.query.column).i64(response.query.lo).i64(response.query.hi);
    w.u32(static_cast<std::uint32_t>(response.range.entries.size()));
    for (const ait::ProofEntry& e : response.range.entries) write_entry(w, e);
    w.u32(static_cast<std::uint32_t>(response.rows.size()));
    for (const RowProof& row : response.rows) write_row(w, row);
    return w.data();
}

/*
 * Implementacion: lee campo por campo con BinaryReader (que revisa cada
 * longitud) y exige que no sobren bytes.
 */
SelectionResponse decode_response(const std::vector<unsigned char>& data) {
    SelectionResponse response;
    try {
        io::BinaryReader r(data.data(), data.size());
        if (r.remaining() < sizeof kMagic ||
            !std::equal(std::begin(kMagic), std::end(kMagic), r.raw(sizeof kMagic))) {
            throw ResponseFormatError("no es una respuesta de tt-integridb");
        }
        if (r.u32() != kFormatVersion) throw ResponseFormatError("version de formato de respuesta desconocida");
        if (r.u8() != kKindSelection) throw ResponseFormatError("tipo de respuesta desconocido");
        response.state_id = r.digest();
        response.query.table = r.text(kMaxName);
        response.query.column = r.text(kMaxName);
        response.query.lo = r.i64();
        response.query.hi = r.i64();

        const std::uint32_t entries = r.u32();
        r.require_items(entries, kEntryBytes);
        response.range.entries.reserve(entries);
        for (std::uint32_t i = 0; i < entries; ++i) response.range.entries.push_back(read_entry(r));

        const std::uint32_t rows = r.u32();
        r.require_items(rows, kRowBytes);
        response.rows.reserve(rows);
        for (std::uint32_t i = 0; i < rows; ++i) response.rows.push_back(read_row(r));
        r.expect_end();
    } catch (const io::FormatError& e) {
        throw ResponseFormatError(std::string("formato invalido: ") + e.what());
    }
    return response;
}

/*
 * Implementacion: codifica y escribe de forma atomica.
 */
std::size_t save_response(const std::filesystem::path& path, const SelectionResponse& response) {
    const std::vector<unsigned char> data = encode_response(response);
    io::write_file_atomic(path, data);
    return data.size();
}

/*
 * Implementacion: lee el archivo y lo decodifica; agrega la ruta a los
 * mensajes de error.
 */
SelectionResponse load_response(const std::filesystem::path& path) {
    const std::vector<unsigned char> data = io::read_file(path);
    try {
        return decode_response(data);
    } catch (const ResponseFormatError& e) {
        throw ResponseFormatError(path.string() + ": " + e.what());
    }
}

}  // namespace tt::query

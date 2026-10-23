// response_io.cpp

#include "tt/query/response_io.hpp"

#include <algorithm>
#include <iterator>
#include <initializer_list>
#include <string>

#include "tt/io/binary.hpp"
#include "tt/io/file.hpp"

namespace tt::query {

namespace {

constexpr unsigned char kMagic[8] = {'T', 'T', 'R', 'S', 'P', 0, 0, 0};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint8_t kKindSelection = 1;
constexpr std::uint8_t kKindMultirange = 2;
constexpr std::uint8_t kKindCount = 3;
constexpr std::uint8_t kKindAverage = 4;
constexpr std::uint8_t kKindJoin = 5;
constexpr std::uint8_t kKindJoinCourse = 6;  // C8, separado del tipo C9
constexpr std::uint32_t kMaxCountReferences = 1000000; // limite de proteccion de recursos
constexpr std::size_t kMaxWitnesses = 4096;

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


/*
 * Implementacion del formato TTRSP tipo 2: conserva las primitivas de
 * escritura usadas por C1-C6 y agrega las dos pruebas AIT y testigos ASO.
 */
std::vector<unsigned char> encode_multirange_response(const MultiRangeResponse& resp) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u32(kFormatVersion).u8(kKindMultirange).digest(resp.state_id);
    w.text(resp.query.first.table);
    for (const auto& q : {resp.query.first, resp.query.second}) {
        w.text(q.column).i64(q.lo).i64(q.hi);
    }
    for (const auto& proof : resp.ranges) {
        w.u32(static_cast<std::uint32_t>(proof.entries.size()));
        for (const auto& e : proof.entries) write_entry(w, e);
    }
    for (const auto& witnesses : resp.splits) {
        w.u32(static_cast<std::uint32_t>(witnesses.size()));
        for (const auto& s : witnesses) w.point(s.rest_g1).point(s.rest_g2);
    }
    w.u32(static_cast<std::uint32_t>(resp.disjoint.size()));
    for (const auto& d : resp.disjoint) w.point(d.bezout_u).point(d.bezout_v);
    w.u32(static_cast<std::uint32_t>(resp.rows.size()));
    for (const auto& row : resp.rows) write_row(w, row);
    return w.data();
}

/*
 * Implementacion: lee el formato C7 y rechaza versiones o tipos distintos,
 * longitudes excesivas, respuestas truncadas y bytes sobrantes.
 */
MultiRangeResponse decode_multirange_response(const std::vector<unsigned char>& bytes) {
    MultiRangeResponse resp;
    try {
        io::BinaryReader r(bytes.data(), bytes.size());
        if (r.remaining() < sizeof kMagic ||
            !std::equal(std::begin(kMagic), std::end(kMagic), r.raw(sizeof kMagic))) {
            throw ResponseFormatError("no es una respuesta de tt-integridb");
        }
        if (r.u32() != kFormatVersion) throw ResponseFormatError("version de formato desconocida");
        if (r.u8() != kKindMultirange) throw ResponseFormatError("la respuesta no es de tipo C7");
        resp.state_id = r.digest();
        const std::string table = r.text(kMaxName);
        for (int k = 0; k < 2; ++k) {
            auto& q = k == 0 ? resp.query.first : resp.query.second;
            q.table = table;
            q.column = r.text(kMaxName);
            q.lo = r.i64();
            q.hi = r.i64();
        }
        for (auto& proof : resp.ranges) {
            const std::uint32_t n = r.u32();
            r.require_items(n, kEntryBytes);
            proof.entries.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) proof.entries.push_back(read_entry(r));
        }
        for (auto& splits : resp.splits) {
            const std::uint32_t n = r.u32();
            if (n > kMaxWitnesses) throw io::FormatError("demasiados testigos de nodos ASO");
            r.require_items(n, 65 + 129);
            splits.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) splits.push_back({r.g1(), r.g2()});
        }
        const std::uint32_t d = r.u32();
        if (d > kMaxWitnesses) throw io::FormatError("demasiados testigos de coprimalidad");
        r.require_items(d, 129 * 2);
        resp.disjoint.reserve(d);
        for (std::uint32_t i = 0; i < d; ++i) resp.disjoint.push_back({r.g2(), r.g2()});
        const std::uint32_t n = r.u32();
        r.require_items(n, kRowBytes);
        resp.rows.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) resp.rows.push_back(read_row(r));
        r.expect_end();
    } catch (const io::FormatError& e) {
        throw ResponseFormatError(std::string("formato C7 invalido: ") + e.what());
    }
    return resp;
}

/*
 * Implementacion: serializa el formato C7 y utiliza escritura atomica.
 */
std::size_t save_multirange_response(const std::filesystem::path& path, const MultiRangeResponse& r) {
    const auto bytes = encode_multirange_response(r);
    io::write_file_atomic(path, bytes);
    return bytes.size();
}

/*
 * Implementacion: lee bytes del archivo, decodifica el tipo 2 y agrega la
 * ruta al mensaje de error cuando el formato es invalido.
 */
MultiRangeResponse load_multirange_response(const std::filesystem::path& path) {
    const auto bytes = io::read_file(path);
    try {
        return decode_multirange_response(bytes);
    } catch (const ResponseFormatError& e) {
        throw ResponseFormatError(path.string() + ": " + e.what());
    }
}


/*
 * Formato tipo 3: digest de estado, C11 o C12, 2 predicados, COUNT, IDs unicos,
 * pruebas RangeCover, asignaciones de ID por nodo y testigos ASO.
 */
std::vector<unsigned char> encode_count_response(const CountResponse& resp) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u32(kFormatVersion).u8(kKindCount).digest(resp.state_id);
    w.text(resp.query.catalog_id).text(resp.query.first.table);
    for (const auto& q : {resp.query.first, resp.query.second}) {
        w.text(q.column).i64(q.lo).i64(q.hi);
    }
    w.u64(resp.count);
    w.u32(static_cast<std::uint32_t>(resp.references.size()));
    for (auto ref : resp.references) w.i64(ref);
    for (const auto& proof : resp.ranges) {
        w.u32(static_cast<std::uint32_t>(proof.entries.size()));
        for (const auto& e : proof.entries) write_entry(w, e);
    }
    for (int k = 0; k < 2; ++k) {
        w.u32(static_cast<std::uint32_t>(resp.node_references[k].size()));
        for (const auto& refs : resp.node_references[k]) {
            w.u32(static_cast<std::uint32_t>(refs.size()));
            for (auto ref : refs) w.i64(ref);
        }
        w.u32(static_cast<std::uint32_t>(resp.splits[k].size()));
        for (const auto& witness : resp.splits[k]) w.point(witness.rest_g1).point(witness.rest_g2);
    }
    w.u32(static_cast<std::uint32_t>(resp.disjoint.size()));
    for (const auto& witness : resp.disjoint) w.point(witness.bezout_u).point(witness.bezout_v);
    return w.data();
}

CountResponse decode_count_response(const std::vector<unsigned char>& bytes) {
    CountResponse resp;
    try {
        io::BinaryReader r(bytes.data(), bytes.size());
        if (r.remaining() < sizeof kMagic ||
            !std::equal(std::begin(kMagic), std::end(kMagic), r.raw(sizeof kMagic))) {
            throw ResponseFormatError("no es una respuesta de tt-integridb");
        }
        if (r.u32() != kFormatVersion) throw ResponseFormatError("version de formato desconocida");
        if (r.u8() != kKindCount) throw ResponseFormatError("la respuesta no es de tipo COUNT");
        resp.state_id = r.digest();
        resp.query.catalog_id = r.text(kMaxName);
        const auto table = r.text(kMaxName);
        for (auto* q : {&resp.query.first, &resp.query.second}) {
            q->table = table;
            q->column = r.text(kMaxName);
            q->lo = r.i64();
            q->hi = r.i64();
        }
        resp.count = r.u64();
        const std::uint32_t count = r.u32();
        if (count > kMaxCountReferences) throw io::FormatError("demasiados ID en COUNT");
        r.require_items(count, sizeof(std::int64_t));
        resp.references.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) resp.references.push_back(r.i64());
        for (auto& proof : resp.ranges) {
            const auto n = r.u32();
            r.require_items(n, kEntryBytes);
            proof.entries.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) proof.entries.push_back(read_entry(r));
        }
        for (int k = 0; k < 2; ++k) {
            const auto nc = r.u32();
            if (nc > kMaxWitnesses) throw io::FormatError("demasiados nodos de cobertura COUNT");
            r.require_items(nc, sizeof(std::uint32_t));
            resp.node_references[k].reserve(nc);
            std::uint64_t total = 0;
            for (std::uint32_t i = 0; i < nc; ++i) {
                const auto n = r.u32();
                total += n;
                if (total > kMaxCountReferences) throw io::FormatError("demasiados ID de nodos COUNT");
                r.require_items(n, sizeof(std::int64_t));
                auto& refs = resp.node_references[k].emplace_back();
                refs.reserve(n);
                for (std::uint32_t j = 0; j < n; ++j) refs.push_back(r.i64());
            }
            const auto ns = r.u32();
            if (ns > kMaxWitnesses) throw io::FormatError("demasiados testigos ASO COUNT");
            r.require_items(ns, 65 + 129);
            resp.splits[k].reserve(ns);
            for (std::uint32_t i = 0; i < ns; ++i) resp.splits[k].push_back({r.g1(), r.g2()});
        }
        const auto nd = r.u32();
        if (nd > kMaxWitnesses) throw io::FormatError("demasiados testigos Bezout COUNT");
        r.require_items(nd, 2 * 129);
        resp.disjoint.reserve(nd);
        for (std::uint32_t i = 0; i < nd; ++i) resp.disjoint.push_back({r.g2(), r.g2()});
        r.expect_end();
    } catch (const io::FormatError& e) {
        throw ResponseFormatError(std::string("formato COUNT invalido: ") + e.what());
    }
    return resp;
}

std::size_t save_count_response(const std::filesystem::path& path, const CountResponse& r) {
    const auto bytes = encode_count_response(r);
    io::write_file_atomic(path, bytes);
    return bytes.size();
}

CountResponse load_count_response(const std::filesystem::path& path) {
    const auto bytes = io::read_file(path);
    try {
        return decode_count_response(bytes);
    } catch (const ResponseFormatError& e) {
        throw ResponseFormatError(path.string() + ": " + e.what());
    }
}

// Formato compacto C10, preservando tipos TTRSP 1-3.
std::vector<unsigned char> encode_average_response(const AverageResponse& resp) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u32(kFormatVersion).u8(kKindAverage).digest(resp.state_id);
    w.i64(resp.student_id).u64(resp.count).i64(resp.sum);
    w.u32(static_cast<std::uint32_t>(resp.range.entries.size()));
    for (const auto& entry : resp.range.entries) write_entry(w, entry);
    w.raw(resp.proof.a0.data(), resp.proof.a0.size());
    w.raw(resp.proof.a1.data(), resp.proof.a1.size());
    w.point(resp.proof.w1).point(resp.proof.w2);
    return w.data();
}
AverageResponse decode_average_response(const std::vector<unsigned char>& bytes) {
    AverageResponse resp;
    try {
        io::BinaryReader r(bytes.data(), bytes.size());
        if (r.remaining() < sizeof kMagic ||
            !std::equal(std::begin(kMagic), std::end(kMagic), r.raw(sizeof kMagic)))
            throw ResponseFormatError("no es respuesta TTRSP");
        if (r.u32() != kFormatVersion || r.u8() != kKindAverage)
            throw ResponseFormatError("formato o tipo C10 invalido");
        resp.state_id = r.digest();
        resp.student_id = r.i64();
        resp.count = r.u64();
        resp.sum = r.i64();
        const auto count = r.u32();
        r.require_items(count, kEntryBytes);
        resp.range.entries.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) resp.range.entries.push_back(read_entry(r));
        const auto* a0 = r.raw(crypto::kScalarBytes);
        std::copy(a0, a0 + crypto::kScalarBytes, resp.proof.a0.begin());
        const auto* a1 = r.raw(crypto::kScalarBytes);
        std::copy(a1, a1 + crypto::kScalarBytes, resp.proof.a1.begin());
        resp.proof.w1 = r.g1();
        resp.proof.w2 = r.g1();
        r.expect_end();
    } catch (const io::FormatError& e) {
        throw ResponseFormatError(std::string("formato C10 invalido: ") + e.what());
    }
    return resp;
}
std::size_t save_average_response(const std::filesystem::path& path, const AverageResponse& resp) {
    const auto data = encode_average_response(resp);
    io::write_file_atomic(path, data);
    return data.size();
}
AverageResponse load_average_response(const std::filesystem::path& path) {
    const auto data = io::read_file(path);
    try { return decode_average_response(data); }
    catch (const ResponseFormatError& e) { throw ResponseFormatError(path.string()+": "+e.what()); }
}


/*
 * Serializa C9: RangeCover total de ambos AIT, separaciones ASO,
 * certificados de Bezout y filas autenticables de dos tablas.
 *
 * Parametros:
 *   resp  respuesta de C9 generada por el Prover
 *
 * Devuelve:
 *   Bytes del formato TTRSP tipo 5.
 */
std::vector<unsigned char> encode_join_program_response(const JoinProgramResponse& resp) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u32(kFormatVersion).u8(kKindJoin).digest(resp.state_id);
    for (const auto& range : resp.ranges) {
        w.u32(static_cast<std::uint32_t>(range.entries.size()));
        for (const auto& entry : range.entries) write_entry(w, entry);
    }
    for (const auto& split : resp.splits) w.point(split.rest_g1).point(split.rest_g2);
    for (const auto& disjoint : resp.disjoint) w.point(disjoint.bezout_u).point(disjoint.bezout_v);
    for (const auto* rows : {&resp.student_rows, &resp.program_rows}) {
        w.u32(static_cast<std::uint32_t>(rows->size()));
        for (const auto& row : *rows) write_row(w, row);
    }
    return w.data();
}

/*
 * Reconstruye C9 desde datos hostiles, sin considerar validas las pruebas.
 *
 * Parametros:
 *   bytes  mensaje recibido de la red o de un archivo
 *
 * Devuelve:
 *   Respuesta decodificada para verify_join_program.
 *
 * Errores:
 *   Lanza ResponseFormatError ante estructuras malformadas.
 */
JoinProgramResponse decode_join_program_response(const std::vector<unsigned char>& bytes) {
    JoinProgramResponse resp;
    try {
        io::BinaryReader r(bytes.data(), bytes.size());
        if (r.remaining() < sizeof kMagic ||
            !std::equal(std::begin(kMagic), std::end(kMagic), r.raw(sizeof kMagic)))
            throw ResponseFormatError("no es una respuesta de tt-integridb");
        if (r.u32() != kFormatVersion || r.u8() != kKindJoin)
            throw ResponseFormatError("formato o tipo C9 invalido");
        resp.state_id = r.digest();
        for (auto& range : resp.ranges) {
            const auto n = r.u32();
            r.require_items(n, kEntryBytes);
            range.entries.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) range.entries.push_back(read_entry(r));
        }
        for (auto& split : resp.splits) split = {r.g1(), r.g2()};
        for (auto& d : resp.disjoint) d = {r.g2(), r.g2()};
        for (auto* rows : {&resp.student_rows, &resp.program_rows}) {
            const auto n = r.u32();
            if (n > kMaxCountReferences) throw io::FormatError("C9: demasiadas filas");
            r.require_items(n, kRowBytes);
            rows->reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) rows->push_back(read_row(r));
        }
        r.expect_end();
    } catch (const io::FormatError& e) {
        throw ResponseFormatError(std::string("formato C9 invalido: ") + e.what());
    }
    return resp;
}

/*
 * Guarda C9 como archivo atomico, listo para comunicacion futura por red.
 *
 * Parametros:
 *   path  archivo de respuesta
 *   resp  resultado y pruebas del Prover
 *
 * Devuelve:
 *   Cantidad de bytes escritos.
 */
std::size_t save_join_program_response(const std::filesystem::path& path, const JoinProgramResponse& resp) {
    const auto bytes = encode_join_program_response(resp);
    io::write_file_atomic(path, bytes);
    return bytes.size();
}

/*
 * Carga el archivo de C9 y verifica su formato binario.
 *
 * Parametros:
 *   path  ruta del archivo de respuesta
 *
 * Devuelve:
 *   Respuesta no confiable lista para comprobacion criptografica.
 *
 * Errores:
 *   Propaga errores de lectura y ResponseFormatError al decodificar.
 */
JoinProgramResponse load_join_program_response(const std::filesystem::path& path) {
    const auto bytes = io::read_file(path);
    try { return decode_join_program_response(bytes); }
    catch (const ResponseFormatError& e) { throw ResponseFormatError(path.string() + ": " + e.what()); }
}

/*
 * Serializa C8: RangeCover total de ambos AIT, separaciones ASO,
 * certificados de Bezout y filas autenticables de dos tablas.
 *
 * Parametros:
 *   resp  respuesta de C8 generada por el Prover
 *
 * Devuelve:
 *   Bytes del formato TTRSP tipo 5.
 */
std::vector<unsigned char> encode_join_course_response(const JoinCourseResponse& resp) {
    io::BinaryWriter w;
    w.raw(kMagic, sizeof kMagic).u32(kFormatVersion).u8(kKindJoinCourse).digest(resp.state_id);
    for (const auto& range : resp.ranges) {
        w.u32(static_cast<std::uint32_t>(range.entries.size()));
        for (const auto& entry : range.entries) write_entry(w, entry);
    }
    for (const auto& split : resp.splits) w.point(split.rest_g1).point(split.rest_g2);
    for (const auto& disjoint : resp.disjoint) w.point(disjoint.bezout_u).point(disjoint.bezout_v);
    for (const auto* rows : {&resp.record_rows, &resp.course_rows}) {
        w.u32(static_cast<std::uint32_t>(rows->size()));
        for (const auto& row : *rows) write_row(w, row);
    }
    return w.data();
}

/*
 * Reconstruye C8 desde datos hostiles, sin considerar validas las pruebas.
 *
 * Parametros:
 *   bytes  mensaje recibido de la red o de un archivo
 *
 * Devuelve:
 *   Respuesta decodificada para verify_join_course.
 *
 * Errores:
 *   Lanza ResponseFormatError ante estructuras malformadas.
 */
JoinCourseResponse decode_join_course_response(const std::vector<unsigned char>& bytes) {
    JoinCourseResponse resp;
    try {
        io::BinaryReader r(bytes.data(), bytes.size());
        if (r.remaining() < sizeof kMagic ||
            !std::equal(std::begin(kMagic), std::end(kMagic), r.raw(sizeof kMagic)))
            throw ResponseFormatError("no es una respuesta de tt-integridb");
        if (r.u32() != kFormatVersion || r.u8() != kKindJoinCourse)
            throw ResponseFormatError("formato o tipo C8 invalido");
        resp.state_id = r.digest();
        for (auto& range : resp.ranges) {
            const auto n = r.u32();
            r.require_items(n, kEntryBytes);
            range.entries.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) range.entries.push_back(read_entry(r));
        }
        for (auto& split : resp.splits) split = {r.g1(), r.g2()};
        for (auto& d : resp.disjoint) d = {r.g2(), r.g2()};
        for (auto* rows : {&resp.record_rows, &resp.course_rows}) {
            const auto n = r.u32();
            if (n > kMaxCountReferences) throw io::FormatError("C8: demasiadas filas");
            r.require_items(n, kRowBytes);
            rows->reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) rows->push_back(read_row(r));
        }
        r.expect_end();
    } catch (const io::FormatError& e) {
        throw ResponseFormatError(std::string("formato C8 invalido: ") + e.what());
    }
    return resp;
}

/*
 * Guarda C8 como archivo atomico, listo para comunicacion futura por red.
 *
 * Parametros:
 *   path  archivo de respuesta
 *   resp  resultado y pruebas del Prover
 *
 * Devuelve:
 *   Cantidad de bytes escritos.
 */
std::size_t save_join_course_response(const std::filesystem::path& path, const JoinCourseResponse& resp) {
    const auto bytes = encode_join_course_response(resp);
    io::write_file_atomic(path, bytes);
    return bytes.size();
}

/*
 * Carga el archivo de C8 y verifica su formato binario.
 *
 * Parametros:
 *   path  ruta del archivo de respuesta
 *
 * Devuelve:
 *   Respuesta no confiable lista para comprobacion criptografica.
 *
 * Errores:
 *   Propaga errores de lectura y ResponseFormatError al decodificar.
 */
JoinCourseResponse load_join_course_response(const std::filesystem::path& path) {
    const auto bytes = io::read_file(path);
    try { return decode_join_course_response(bytes); }
    catch (const ResponseFormatError& e) { throw ResponseFormatError(path.string() + ": " + e.what()); }
}

}  // namespace tt::query

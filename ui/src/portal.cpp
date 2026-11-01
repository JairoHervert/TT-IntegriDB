// portal.cpp

#include "tt/ui/portal.hpp"

#include <chrono>
#include <exception>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "tt/crypto/hash.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/query/query.hpp"
#include "tt/query/verifier_service.hpp"
#include "tt/ui/json.hpp"

namespace tt::ui {

namespace {

using Clock = std::chrono::steady_clock;

/*
 * Segundos transcurridos desde un instante del reloj monotono.
 */
double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/*
 * Indica si el texto solo tiene ASCII imprimible y espacios (la misma regla
 * que aplica el probador a las consultas que recibe).
 */
bool plain_text(const std::string& text) {
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(u >= 0x20 && u < 0x7f) && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}

// Una consulta ya leida y revisada contra delta, o el motivo por el que no
// se puede enviar.
struct Prepared {
    std::string text;                      // palabras unidas con un espacio
    std::optional<query::ParsedQuery> parsed;
    std::vector<scheme::TreeSpec> trees;
    int status = 200;                      // 400 o 422 si no se puede enviar
    std::string outcome;                   // invalid o unsupported si no se puede enviar
    std::string message;                   // motivo
};

/*
 * Lee la consulta y revisa que delta tenga sus AIT. Es el mismo orden que
 * sigue tt_verifier query: nada se envia al probador si no se podria
 * verificar.
 */
Prepared prepare(const std::string& body, const scheme::DatabaseDigest& digest) {
    Prepared p;
    if (!plain_text(body)) {
        p.text = net::safe_text(body, 120);
        p.status = 400;
        p.outcome = "invalid";
        p.message = "la consulta debe ser texto ASCII imprimible";
        return p;
    }
    const std::vector<std::string> words = net::split_words(body);
    p.text = net::join_words(words);
    try {
        p.parsed = query::parse_query(words);
    } catch (const query::QueryError& e) {
        p.status = 400;
        p.outcome = "invalid";
        p.message = e.what();
        return p;
    }
    try {
        p.trees = query::required_trees(*p.parsed, digest);
    } catch (const query::UnsupportedQuery& e) {
        p.status = 422;
        p.outcome = "unsupported";
        p.message = e.what();
    }
    return p;
}

/*
 * Escribe los campos que describen la consulta: texto, id del catalogo, SQL
 * y los AIT con que se verifica.
 */
void write_query_fields(JsonWriter& w, const Prepared& p) {
    w.key("query").value(p.text);
    w.key("catalog_id").value(p.parsed ? p.parsed->catalog_id : std::string());
    w.key("sql").value(p.parsed ? query::to_sql(*p.parsed) : std::string());
    w.key("trees").begin_array();
    for (const scheme::TreeSpec& t : p.trees) w.value(t.label());
    w.end_array();
}

/*
 * Respuesta para una consulta que no se envia al probador (mal escrita o no
 * soportada).
 */
ApiReply not_sent(const Prepared& p) {
    JsonWriter w;
    w.begin_object();
    write_query_fields(w, p);
    w.key("outcome").value(p.outcome);
    w.key("message").value(p.message);
    w.end_object();
    return {p.status, w.str()};
}

/*
 * Escribe un valor del resultado con su tipo de JSON.
 */
void write_value(JsonWriter& w, const query::ResultValue& v) {
    if (std::holds_alternative<std::monostate>(v)) {
        w.null();
    } else if (const auto* i = std::get_if<std::int64_t>(&v)) {
        w.value(*i);
    } else if (const auto* d = std::get_if<double>(&v)) {
        w.value(*d);
    } else {
        w.value(std::get<std::string>(v));
    }
}

/*
 * Escribe la tabla del resultado (a lo mas max_rows filas).
 */
void write_result(JsonWriter& w, const query::ResultTable& table, std::size_t max_rows) {
    const std::size_t shown = table.rows.size() < max_rows ? table.rows.size() : max_rows;
    w.key("result").begin_object();
    w.key("columns").begin_array();
    for (const std::string& c : table.columns) w.value(c);
    w.end_array();
    w.key("row_count").value(static_cast<std::uint64_t>(table.rows.size()));
    w.key("truncated").value(shown < table.rows.size());
    w.key("rows").begin_array();
    for (std::size_t r = 0; r < shown; ++r) {
        w.begin_array();
        for (const query::ResultValue& v : table.rows[r]) write_value(w, v);
        w.end_array();
    }
    w.end_array();
    w.end_object();
}

}  // namespace

/*
 * Implementacion: guarda referencias; delta y la clave ya se cargaron y
 * comprobaron en quien crea el Portal.
 */
Portal::Portal(const scheme::DatabaseDigest& digest, const aso::PublicKey& public_key, PortalOptions options,
               QueryLogger logger)
    : digest_(digest),
      public_key_(public_key),
      options_(std::move(options)),
      logger_(std::move(logger)),
      state_id_(crypto::to_hex(digest.id())) {}

/*
 * Implementacion: la parte local sale de delta; la del probador, de
 * /v1/health con tiempos cortos para que la pagina no se quede esperando.
 * Las columnas de rango son las que tienen un AIT hacia la columna de
 * referencia de su tabla (las que acepta selection_tree).
 */
ApiReply Portal::status() const {
    JsonWriter w;
    w.begin_object();
    w.key("client").begin_object();
    w.key("state_id").value(state_id_);
    w.key("state_version").value(static_cast<std::uint64_t>(digest_.state_version));
    w.key("plan").value(digest_.plan);
    w.key("tables").begin_array();
    for (const scheme::TableDigest& t : digest_.tables) {
        w.begin_object();
        w.key("name").value(t.schema.name);
        w.key("rows").value(static_cast<std::uint64_t>(t.row_count));
        w.end_object();
    }
    w.end_array();
    w.key("range_columns").begin_array();
    for (const scheme::TreeDigest& t : digest_.trees) {
        try {
            const query::RangeQuery probe{t.spec.table, t.spec.key_column, 0, 0};
            if (query::selection_tree(probe, digest_) == t.spec) {
                w.begin_object();
                w.key("table").value(t.spec.table);
                w.key("column").value(t.spec.key_column);
                w.end_object();
            }
        } catch (const query::UnsupportedQuery&) {
            // AIT de un JOIN o de un agregado: no sirve para un rango simple.
        }
    }
    w.end_array();
    w.end_object();

    w.key("prover").begin_object();
    w.key("address").value(options_.prover.text());
    try {
        const net::HealthInfo info = net::fetch_health(options_.prover, options_.health);
        const std::string id = crypto::to_hex(info.state_id);
        w.key("reachable").value(true);
        w.key("state_id").value(id);
        w.key("state_version").value(static_cast<std::uint64_t>(info.state_version));
        w.key("plan").value(info.plan);
        w.key("public_key").value(info.public_key);
        w.key("matches").value(id == state_id_);
    } catch (const std::exception& e) {
        w.key("reachable").value(false);
        w.key("matches").value(false);
        w.key("message").value(net::safe_text(e.what()));
    }
    w.end_object();
    w.end_object();
    return {200, w.str()};
}

/*
 * Implementacion: solo prepare(); no hay red.
 */
ApiReply Portal::preview(const std::string& text) const {
    const Prepared p = prepare(text, digest_);
    if (p.status != 200) return not_sent(p);
    JsonWriter w;
    w.begin_object();
    write_query_fields(w, p);
    w.key("outcome").value("ready");
    w.key("message").value("");
    w.end_object();
    return {200, w.str()};
}

/*
 * Implementacion: prepare(), fetch_answer() sin bloqueo (el probador ya
 * atiende de una en una) y verify_query() con el candado, porque es la parte
 * que usa el estado de NTL de este proceso.
 */
ApiReply Portal::query(const std::string& text) {
    const Prepared p = prepare(text, digest_);
    QueryRecord record;
    record.query = p.text;
    if (p.status != 200) {
        record.outcome = p.outcome;
        record.note = p.message;
        if (logger_) logger_(record);
        return not_sent(p);
    }

    JsonWriter w;
    w.begin_object();
    write_query_fields(w, p);
    w.key("state").begin_object();
    w.key("id").value(state_id_);
    w.key("version").value(static_cast<std::uint64_t>(digest_.state_version));
    w.end_object();

    const Clock::time_point start = Clock::now();
    std::vector<unsigned char> bytes;
    int prover_status = 200;
    std::string failure;
    try {
        bytes = net::fetch_answer(options_.prover, p.text, options_.query);
    } catch (const net::ServerError& e) {
        prover_status = e.status();
        failure = "el probador respondio con error (" + std::to_string(e.status()) + "): " + e.what();
    } catch (const net::NetworkError& e) {
        prover_status = 0;
        failure = e.what();
    }
    record.prover_seconds = seconds_since(start);
    record.bytes = bytes.size();

    w.key("prover").begin_object();
    w.key("address").value(options_.prover.text());
    w.key("status").value(prover_status);
    w.key("seconds").value(record.prover_seconds);
    w.key("bytes").value(static_cast<std::uint64_t>(bytes.size()));
    w.end_object();

    if (!failure.empty()) {
        w.key("outcome").value("no_answer");
        w.key("message").value(failure);
        w.end_object();
        record.outcome = "no_answer";
        record.note = failure;
        if (logger_) logger_(record);
        return {200, w.str()};
    }

    query::VerifierOutcome outcome;
    {
        std::lock_guard<std::mutex> lock(verify_mutex_);
        crypto::init_pairing_group();  // fija el modulo de NTL en este hilo
        outcome = query::verify_query(digest_, public_key_, *p.parsed, bytes);
    }
    record.verify_seconds = outcome.seconds;

    w.key("verification").begin_object();
    w.key("seconds").value(outcome.seconds);
    w.key("stages").begin_array();
    for (const auto& stage : outcome.stages) {
        w.begin_object();
        w.key("name").value(stage.first);
        w.key("seconds").value(stage.second);
        w.end_object();
    }
    w.end_array();
    w.end_object();

    if (outcome.accepted) {
        w.key("outcome").value("verified");
        w.key("message").value("");
        w.key("detail").value(outcome.detail);
        write_result(w, outcome.result, options_.max_rows);
        record.outcome = "verified";
        record.note = outcome.detail;
    } else {
        w.key("outcome").value("rejected");
        w.key("message").value(outcome.reason);
        record.outcome = "rejected";
        record.note = outcome.reason;
    }
    w.end_object();
    if (logger_) logger_(record);
    return {200, w.str()};
}

}  // namespace tt::ui

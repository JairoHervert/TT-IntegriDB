// service_test.cpp
//
// Pruebas de los servicios del nucleo que usan los programas:
// query::answer_query (probador) y query::verify_query (verificador).
//   - Las 14 formas del catalogo (y "range") se responden y se verifican, y
//     el resultado coincide con un oraculo que recorre las tablas.
//   - Una respuesta alterada, truncada, de otro tipo o de otra consulta se
//     rechaza sin lanzar excepciones.
//   - Las consultas sin arbol en delta se informan como no soportadas en
//     los dos extremos.

#include <string>
#include <vector>

#include "service_fixture.hpp"
#include "test_support.hpp"
#include "tt/query/query.hpp"

using namespace tt;
using test::check;
using test::check_throws;
using test::section;

namespace {

/*
 * Nombre de una consulta para los mensajes de la prueba.
 */
std::string name_of(const std::vector<std::string>& words) {
    std::string n;
    for (const std::string& w : words) n += (n.empty() ? "" : " ") + w;
    return n;
}

}  // namespace

/*
 * Ejecuta las comprobaciones y devuelve 0 si todas pasan.
 */
int main() {
    test::ServiceFixture fx;
    const query::ProverContext ctx = fx.context();

    section("Clasificacion y texto SQL de las consultas");
    check(query::query_kind(query::parse_query({"C4", "6", "8"})) == query::QueryKind::kSelection, "C4 es seleccion");
    check(query::query_kind(query::parse_query({"C7", "6", "8", "1", "2"})) == query::QueryKind::kMultiRange, "C7 es rango multidimensional");
    check(query::query_kind(query::parse_query({"C11", "1"})) == query::QueryKind::kCount, "C11 es conteo");
    check(query::query_kind(query::parse_query({"C14", "1"})) == query::QueryKind::kSumJoinCourse, "C14 es suma tras JOIN");
    check(query::to_sql(query::parse_query({"C10", "3"})) == "SELECT AVG(grade) FROM academic_record WHERE student_id = 3",
          "to_sql de C10");
    check(!query::needs_public_key(query::parse_query({"C1", "1"})) && query::needs_public_key(query::parse_query({"C8"})),
          "solo C7 a C14 necesitan la clave publica");
    check(query::reads_rows(query::parse_query({"C4", "6", "8"})) && !query::reads_rows(query::parse_query({"C10", "1"})) &&
              !query::reads_rows(query::parse_query({"C12", "1", "1"})),
          "C10 a C12 no leen filas de D");

    section("Las 14 formas del catalogo: respuesta verificada igual al oraculo");
    for (const test::CatalogCase& c : test::catalog_cases()) {
        const query::ParsedQuery q = query::parse_query(c.words);
        const query::ProverReply reply = query::answer_query(ctx, q);
        check(reply.generation_seconds >= 0 && reply.encoding_seconds >= 0 && reply.row_fetch_seconds >= 0 &&
                  reply.row_fetch_seconds <= reply.generation_seconds + 0.01,
              name_of(c.words) + ": tiempos internos y lecturas de D coherentes");
        const query::VerifierOutcome v = query::verify_query(fx.state.digest, fx.keys.pk.aso, q, reply.bytes);
        const test::Expected expected = test::oracle(fx.base, c.words);
        check(v.accepted && test::matches(v.result, expected) && reply.stale_rows == 0,
              name_of(c.words) + ": aceptada y con el resultado correcto" + (v.accepted ? "" : " (" + v.reason + ")"));
        check(!query::required_trees(q, fx.state.digest).empty(), name_of(c.words) + ": tiene arboles en delta");
    }

    section("Medicion de lecturas de RowStore sin alterar ProofGen");
    {
        const query::ParsedQuery selection = query::parse_query({"C4", "6", "8"});
        const query::ProverReply sampled = query::answer_query(ctx, selection);
        check(sampled.row_fetch_calls > 0 && sampled.row_fetch_rows > 0 && sampled.row_fetch_seconds >= 0,
              "las consultas con filas registran acceso a D");
        const query::ParsedQuery average = query::parse_query({"C10", "1"});
        const query::ProverReply no_fetch = query::answer_query(ctx, average);
        check(no_fetch.row_fetch_calls == 0 && no_fetch.row_fetch_seconds == 0,
              "C10 no inventa lecturas de D");
    }

    section("Etapas no solapadas de C8 y C14");
    for (const std::vector<std::string>& words : {std::vector<std::string>{"C8"},
                                                 std::vector<std::string>{"C14", "1"}}) {
        const auto q = query::parse_query(words);
        const auto reply = query::answer_query(ctx, q);
        const double accounted = reply.ait_seconds + reply.merkle_seconds + reply.aso_seconds +
                                 reply.row_fetch_seconds;
        check(reply.detailed_stages && reply.ait_seconds >= 0 && reply.merkle_seconds >= 0 &&
                  reply.aso_seconds >= 0 && accounted <= reply.generation_seconds + 0.01,
              name_of(words) + ": las operaciones medidas no superan ProofGen");
        const auto verified = query::verify_query(fx.state.digest, fx.keys.pk.aso, q, reply.bytes);
        check(verified.accepted && verified.stages.size() == 4,
              name_of(words) + ": Verify conserva el veredicto y publica cuatro etapas");
        if (verified.stages.size() == 4) {
            double sum = 0;
            for (const auto& stage : verified.stages) {
                check(stage.second >= 0, name_of(words) + ": duracion no negativa de " + stage.first);
                sum += stage.second;
            }
            check(sum <= verified.seconds + 0.01,
                  name_of(words) + ": las etapas de Verify no superan su duracion total");
        }
    }

    section("Comprobacion literal del ASO en C1 a C6");
    {
        const query::ParsedQuery q = query::parse_query({"C4", "6", "8"});
        const query::ProverReply reply = query::answer_query(ctx, q);
        query::VerifyOptions opt;
        opt.check_accumulators = true;
        const query::VerifierOutcome v = query::verify_query(fx.state.digest, fx.keys.pk.aso, q, reply.bytes, opt);
        check(v.accepted && v.stages.size() == 3, "C4 con --aso se acepta y mide tres etapas");
    }

    section("Respuestas alteradas: se rechazan sin excepciones");
    for (const test::CatalogCase& c : test::catalog_cases()) {
        const query::ParsedQuery q = query::parse_query(c.words);
        const std::vector<unsigned char> honest = query::answer_query(ctx, q).bytes;
        bool all_rejected = true;
        // Se altera un byte en varias posiciones (encabezado, estado, prueba y final).
        for (std::size_t pos : {std::size_t{9}, std::size_t{20}, honest.size() / 3, honest.size() / 2, honest.size() - 1}) {
            std::vector<unsigned char> bad = honest;
            bad[pos] ^= 0x01;
            all_rejected = all_rejected && !query::verify_query(fx.state.digest, fx.keys.pk.aso, q, bad).accepted;
        }
        std::vector<unsigned char> truncated(honest.begin(), honest.begin() + honest.size() / 2);
        std::vector<unsigned char> extra = honest;
        extra.push_back(0);
        all_rejected = all_rejected && !query::verify_query(fx.state.digest, fx.keys.pk.aso, q, truncated).accepted &&
                       !query::verify_query(fx.state.digest, fx.keys.pk.aso, q, extra).accepted &&
                       !query::verify_query(fx.state.digest, fx.keys.pk.aso, q, {}).accepted;
        check(all_rejected, name_of(c.words) + ": bytes alterados, truncados, sobrantes o vacios se rechazan");
    }

    section("Respuesta de otra consulta o de otro tipo");
    {
        const auto ask = [&](std::vector<std::string> words) { return query::parse_query(words); };
        const std::vector<unsigned char> c4 = query::answer_query(ctx, ask({"C4", "6", "8"})).bytes;
        const std::vector<unsigned char> c10 = query::answer_query(ctx, ask({"C10", "1"})).bytes;
        const std::vector<unsigned char> c13 = query::answer_query(ctx, ask({"C13", "1"})).bytes;
        const std::vector<unsigned char> c8 = query::answer_query(ctx, ask({"C8"})).bytes;
        const auto rejected = [&](const std::vector<std::string>& words, const std::vector<unsigned char>& bytes) {
            return !query::verify_query(fx.state.digest, fx.keys.pk.aso, ask(words), bytes).accepted;
        };
        check(rejected({"C4", "6", "9"}, c4), "la respuesta de C4 6 8 no sirve para C4 6 9");
        check(rejected({"C5", "20241", "20242"}, c4), "la respuesta de C4 no sirve para C5");
        check(rejected({"C10", "2"}, c10), "la respuesta de C10 1 no sirve para C10 2");
        check(rejected({"C11", "1"}, c10), "una respuesta de tipo AVG no sirve para COUNT");
        check(rejected({"C7", "5", "10", "20241", "20252"}, c13), "la respuesta de C13 (tipo 2) no sirve para C7");
        check(rejected({"C9"}, c8), "la respuesta de C8 no sirve para C9");
    }

    section("Respuesta de otro estado (RNF08)");
    {
        test::ServiceFixture other(60);
        const query::ParsedQuery q = query::parse_query({"C4", "6", "8"});
        const std::vector<unsigned char> bytes = query::answer_query(other.context(), q).bytes;
        check(!query::verify_query(fx.state.digest, fx.keys.pk.aso, q, bytes).accepted,
              "una respuesta generada con otro delta se rechaza");
    }

    section("Consultas no soportadas y contexto incompleto");
    {
        const query::ParsedQuery q = query::parse_query({"range", "student", "program_id", "1", "2"});
        check_throws<query::UnsupportedQuery>([&] { query::required_trees(q, fx.state.digest); },
                                              "el verificador detecta que no hay AIT program_id -> student_id");
        check_throws<query::UnsupportedQuery>([&] { query::answer_query(ctx, q); },
                                              "el probador tampoco la responde");
        query::ProverContext no_pk = ctx;
        no_pk.public_key = nullptr;
        check_throws<query::ProverError>([&] { query::answer_query(no_pk, query::parse_query({"C7", "6", "8", "20241", "20242"})); },
                                         "sin clave publica, C7 se informa como error del probador");
        check(query::verify_query(fx.state.digest, fx.keys.pk.aso, query::parse_query({"C4", "6", "8"}),
                                  query::answer_query(no_pk, query::parse_query({"C4", "6", "8"})).bytes)
                  .accepted,
              "sin clave publica, C4 se sigue respondiendo");
        query::ProverContext no_store = ctx;
        no_store.store = nullptr;
        check_throws<query::ProverError>([&] { query::answer_query(no_store, query::parse_query({"C2", "1"})); },
                                         "sin acceso a D, C2 se informa como error del probador");
        check(query::verify_query(fx.state.digest, fx.keys.pk.aso, query::parse_query({"C12", "1", "1"}),
                                  query::answer_query(no_store, query::parse_query({"C12", "1", "1"})).bytes)
                  .accepted,
              "sin acceso a D, C12 se sigue respondiendo (no lee filas)");
    }

    section("Base modificada despues de Setup");
    {
        db::Database changed = fx.base;
        db::Table* ar = nullptr;
        for (db::Table& t : changed.tables) {
            if (t.schema.name == "academic_record") ar = &t;
        }
        std::int64_t record = 0;
        for (db::Row& r : ar->rows) {
            if (test::at(r, 5) >= 6 && test::at(r, 5) <= 7) {
                r[5] = test::at(r, 5) + 1;  // sigue dentro de [6, 8]
                record = test::at(r, 0);
                break;
            }
        }
        db::MemoryRowStore changed_store(changed);
        query::ProverContext tampered = ctx;
        tampered.store = &changed_store;
        const query::ParsedQuery q = query::parse_query({"C4", "6", "8"});
        const query::ProverReply reply = query::answer_query(tampered, q);
        const query::VerifierOutcome v = query::verify_query(fx.state.digest, fx.keys.pk.aso, q, reply.bytes);
        check(record != 0 && reply.stale_rows == 1, "el probador advierte una fila distinta a la de Setup");
        check(!v.accepted && v.reason.find(std::to_string(record)) != std::string::npos,
              "el verificador rechaza e identifica la fila alterada");
    }

    section("Texto de los valores");
    check(query::value_text(query::ResultValue{}) == "NULL", "NULL");
    check(query::value_text(query::ResultValue{std::int64_t{-5}}) == "-5", "entero");
    check(query::value_text(query::ResultValue{7.4150943}) == "7.415094", "decimal con seis cifras");
    check(query::value_text(query::ResultValue{std::string("ISC")}) == "ISC", "texto");

    return test::finish();
}

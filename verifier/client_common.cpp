// client_common.cpp

#include "client_common.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "commands.hpp"
#include "tt/cli/cli.hpp"
#include "tt/crypto/hash.hpp"
#include "tt/scheme/key_io.hpp"

namespace tt::verifier {

namespace fs = std::filesystem;
using cli::Clock;
using cli::seconds_since;

namespace {

/*
 * Convierte el texto de --show en numero (solo digitos, a lo mas 9).
 *
 * Errores:
 *   Lanza cli::UsageError si no es un entero no negativo.
 */
std::size_t parse_show(const std::string& text) {
    if (text.empty() || text.size() > 9 || text.find_first_not_of("0123456789") != std::string::npos) {
        throw cli::UsageError("--show espera un entero no negativo, se recibio '" + text + "'");
    }
    return static_cast<std::size_t>(std::stoul(text));
}

}  // namespace

/*
 * Implementacion: una rama por opcion comun.
 */
bool parse_common_option(const std::vector<std::string>& args, std::size_t& i, CommonOptions& opt) {
    const std::string& arg = args[i];
    if (arg == "--keys") {
        opt.keys = cli::option_value(args, i);
    } else if (arg == "--delta") {
        opt.delta = cli::option_value(args, i);
    } else if (arg == "--show") {
        opt.show = parse_show(cli::option_value(args, i));
    } else if (arg == "--aso") {
        opt.aso = true;
    } else {
        return false;
    }
    return true;
}

/*
 * Implementacion: traduce QueryError a error de uso.
 */
query::ParsedQuery parse_query_words(const std::vector<std::string>& words) {
    try {
        return query::parse_query(words);
    } catch (const query::QueryError& e) {
        throw cli::UsageError(e.what());
    }
}

/*
 * Implementacion: el mismo orden y los mismos mensajes que tenia el comando
 * verify antes de separar esta parte.
 */
int prepare_client(const CommonOptions& opt, ClientState& state) {
    const std::string label = opt.query.catalog_id.empty() ? "consulta" : "consulta " + opt.query.catalog_id;
    std::printf("[verifier] %s: %s\n", label.c_str(), query::to_sql(opt.query).c_str());

    const fs::path pk_path = opt.keys / "public_key.txt";
    if (!fs::exists(opt.delta)) {
        std::fprintf(stderr,
                     "[verifier] no existe el digest %s: indica con --delta el delta.txt que publico el\n"
                     "           propietario (por ejemplo --delta deploy/verifier/delta.txt).\n",
                     opt.delta.string().c_str());
        return 1;
    }
    if (!fs::exists(pk_path)) {
        std::fprintf(stderr,
                     "[verifier] no hay clave publica en %s: indica con --keys la carpeta donde\n"
                     "           esta public_key.txt (la misma que se uso en Setup).\n",
                     opt.keys.string().c_str());
        return 1;
    }
    state.digest = scheme::load_digest(opt.delta);
    std::printf("[verifier] digest delta: %s (version %llu, id %s)\n", opt.delta.string().c_str(),
                static_cast<unsigned long long>(state.digest.state_version), short_id(state.digest.id()).c_str());
    try {
        for (const scheme::TreeSpec& tree : query::required_trees(opt.query, state.digest)) {
            std::printf("[verifier] se verifica con el AIT %s\n", tree.label().c_str());
        }
    } catch (const query::UnsupportedQuery& e) {
        std::fprintf(stderr, "[verifier] consulta no soportada: %s\n", e.what());
        return 1;
    }
    if (opt.aso && query::query_kind(opt.query) != query::QueryKind::kSelection) {
        std::printf("[verifier] --aso solo cambia la verificacion de C1 a C6; en esta consulta el ASO ya es obligatorio\n");
    }

    const auto start = Clock::now();
    state.public_key = scheme::load_public_key(pk_path);
    if (scheme::public_key_fingerprint(state.public_key) != state.digest.key_fingerprint) {
        std::fprintf(stderr,
                     "[verifier] la clave publica de %s no es la del digest delta: usa con --keys la\n"
                     "           carpeta del mismo 'tt_owner init' con el que se ejecuto ese Setup.\n",
                     opt.keys.string().c_str());
        return 1;
    }
    std::printf("[verifier] clave publica cargada y comprobada contra delta en %.2f s\n", seconds_since(start));
    return 0;
}

/*
 * Implementacion: query::verify_query decide; aqui solo se muestra.
 */
int verify_and_report(const CommonOptions& opt, const ClientState& state, const std::vector<unsigned char>& bytes) {
    query::VerifyOptions options;
    options.check_accumulators = opt.aso;
    const query::VerifierOutcome outcome =
        query::verify_query(state.digest, state.public_key.aso, opt.query, bytes, options);
    const std::string id = opt.query.catalog_id.empty() ? std::string() : " " + opt.query.catalog_id;
    if (!outcome.accepted) {
        std::printf("[verifier] RESPUESTA RECHAZADA%s: %s\n", id.c_str(), outcome.reason.c_str());
        return kExitRejected;
    }
    std::printf("[verifier] RESPUESTA VERIFICADA%s: %s en %.2f s\n", id.c_str(), outcome.detail.c_str(),
                outcome.seconds);
    if (!outcome.stages.empty()) {
        std::string stages;
        for (const auto& stage : outcome.stages) {
            char text[128];
            std::snprintf(text, sizeof text, "%s%s %.3f s", stages.empty() ? "" : ", ", stage.first.c_str(),
                          stage.second);
            stages += text;
        }
        std::printf("[verifier]   %s\n", stages.c_str());
    }
    print_table(outcome.result, opt.show);
    return 0;
}

/*
 * Implementacion: calcula el ancho de cada columna con las filas que se
 * muestran y las alinea con " | ".
 */
void print_table(const query::ResultTable& table, std::size_t limit) {
    const std::size_t shown = std::min(limit, table.rows.size());
    if (shown == 0) return;
    std::vector<std::size_t> width(table.columns.size());
    for (std::size_t c = 0; c < table.columns.size(); ++c) width[c] = table.columns[c].size();
    for (std::size_t r = 0; r < shown; ++r) {
        for (std::size_t c = 0; c < width.size() && c < table.rows[r].size(); ++c) {
            width[c] = std::max(width[c], query::value_text(table.rows[r][c]).size());
        }
    }
    auto print_line = [&](auto text_of) {
        std::string line = "  ";
        for (std::size_t c = 0; c < width.size(); ++c) {
            std::string t = text_of(c);
            t.resize(width[c], ' ');
            line += (c ? " | " : "") + t;
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        std::printf("%s\n", line.c_str());
    };
    print_line([&](std::size_t c) { return table.columns[c]; });
    print_line([&](std::size_t c) { return std::string(width[c], '-'); });
    for (std::size_t r = 0; r < shown; ++r) {
        print_line([&](std::size_t c) {
            return c < table.rows[r].size() ? query::value_text(table.rows[r][c]) : std::string();
        });
    }
    if (shown < table.rows.size()) {
        std::printf("  ... (%zu filas mas; usa --show N para ver mas)\n", table.rows.size() - shown);
    }
}

/*
 * Implementacion: los primeros 16 digitos hexadecimales.
 */
std::string short_id(const crypto::Digest& digest) { return crypto::to_hex(digest).substr(0, 16); }

}  // namespace tt::verifier

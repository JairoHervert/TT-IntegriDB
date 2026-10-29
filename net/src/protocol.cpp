// protocol.cpp

#include "tt/net/protocol.hpp"

#include <algorithm>
#include <sstream>

#include "tt/scheme/digest.hpp"

namespace tt::net {

namespace {

// Primera linea del texto de /v1/health.
const char* const kHealthHeader = "tt-integridb prover 1";

/*
 * Indica si un caracter puede formar parte de un nombre de host o de una
 * IPv4: letras, digitos, punto, guion y guion bajo.
 */
bool host_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
           c == '_';
}

/*
 * Lee un entero sin signo escrito solo con digitos (a lo mas 19).
 *
 * Errores:
 *   Lanza std::runtime_error con el mensaje dado si el texto no es valido.
 */
std::uint64_t parse_unsigned(const std::string& text, const std::string& what) {
    if (text.empty() || text.size() > 19 || text.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error(what + " no es un entero valido");
    }
    return std::stoull(text);
}

}  // namespace

/*
 * Implementacion: separa en el ultimo ':' (si lo hay) y revisa cada parte.
 */
Endpoint parse_endpoint(const std::string& text) {
    if (text.find("://") != std::string::npos) {
        throw AddressError("la direccion del probador se escribe como host:puerto, sin 'http://': " + safe_text(text));
    }
    Endpoint e;
    const std::size_t colon = text.rfind(':');
    e.host = colon == std::string::npos ? text : text.substr(0, colon);
    if (e.host.empty() || e.host.size() > 253) throw AddressError("falta el host en la direccion '" + safe_text(text) + "'");
    for (char c : e.host) {
        if (!host_char(c)) throw AddressError("el host '" + safe_text(e.host) + "' tiene caracteres no validos");
    }
    if (colon != std::string::npos) {
        const std::string port = text.substr(colon + 1);
        if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos ||
            std::stoi(port) < 1 || std::stoi(port) > 65535) {
            throw AddressError("el puerto debe ser un entero entre 1 y 65535, se recibio '" + safe_text(port) + "'");
        }
        e.port = std::stoi(port);
    }
    return e;
}

/*
 * Implementacion: encabezado y cuatro lineas "clave valor".
 */
std::string health_to_text(const HealthInfo& info) {
    std::ostringstream out;
    out << kHealthHeader << '\n'
        << "state_id " << crypto::to_hex(info.state_id) << '\n'
        << "state_version " << info.state_version << '\n'
        << "plan " << info.plan << '\n'
        << "public_key " << (info.public_key ? "yes" : "no") << '\n';
    return out.str();
}

/*
 * Implementacion: exige el encabezado y las cuatro claves, cada una una
 * sola vez y en ese orden.
 */
HealthInfo health_from_text(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != kHealthHeader) {
        throw std::runtime_error("el servidor no respondio como un probador de tt-integridb");
    }
    auto value_of = [&](const std::string& key) {
        if (!std::getline(in, line) || line.rfind(key + " ", 0) != 0) {
            throw std::runtime_error("falta '" + key + "' en el estado del probador");
        }
        return line.substr(key.size() + 1);
    };
    HealthInfo info;
    if (!scheme::parse_hex_digest(value_of("state_id"), info.state_id)) {
        throw std::runtime_error("el id de estado del probador no es valido");
    }
    info.state_version = parse_unsigned(value_of("state_version"), "la version de estado del probador");
    info.plan = value_of("plan");
    const std::string pk = value_of("public_key");
    if (pk != "yes" && pk != "no") throw std::runtime_error("valor no valido en public_key");
    info.public_key = pk == "yes";
    if (std::getline(in, line)) throw std::runtime_error("el estado del probador tiene lineas de mas");
    return info;
}

/*
 * Implementacion: reemplaza todo lo que no sea ASCII imprimible y agrega
 * "..." si se recorto.
 */
std::string safe_text(const std::string& text, std::size_t max_chars) {
    std::string out;
    out.reserve(std::min(text.size(), max_chars) + 3);
    for (char c : text) {
        if (out.size() == max_chars) {
            out += "...";
            break;
        }
        const unsigned char u = static_cast<unsigned char>(c);
        out += (u >= 0x20 && u < 0x7f) ? c : '?';
    }
    return out;
}

/*
 * Implementacion: separa por espacios, tabuladores y saltos de linea.
 */
std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!current.empty()) words.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) words.push_back(std::move(current));
    return words;
}

/*
 * Implementacion: une con un espacio entre palabras.
 */
std::string join_words(const std::vector<std::string>& words) {
    std::string text;
    for (const std::string& w : words) {
        if (!text.empty()) text += ' ';
        text += w;
    }
    return text;
}

}  // namespace tt::net

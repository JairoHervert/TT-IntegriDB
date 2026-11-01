// json.cpp

#include "tt/ui/json.hpp"

#include <cmath>
#include <cstdio>

namespace tt::ui {

/*
 * Implementacion: las secuencias cortas de RFC 8259 para los caracteres
 * comunes y \u00XX para el resto de los caracteres de control.
 */
std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (u < 0x20 || u == 0x7f) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(u));
                    out += escaped;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

/*
 * Implementacion: el primer elemento de cada nivel no lleva coma.
 */
void JsonWriter::separate() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (!first_.empty()) {
        if (!first_.back()) out_ += ',';
        first_.back() = false;
    }
}

/*
 * Implementacion: abre un nivel nuevo.
 */
JsonWriter& JsonWriter::begin_object() {
    separate();
    out_ += '{';
    first_.push_back(true);
    return *this;
}

/*
 * Implementacion: cierra el nivel.
 */
JsonWriter& JsonWriter::end_object() {
    out_ += '}';
    if (!first_.empty()) first_.pop_back();
    return *this;
}

/*
 * Implementacion: abre un nivel nuevo.
 */
JsonWriter& JsonWriter::begin_array() {
    separate();
    out_ += '[';
    first_.push_back(true);
    return *this;
}

/*
 * Implementacion: cierra el nivel.
 */
JsonWriter& JsonWriter::end_array() {
    out_ += ']';
    if (!first_.empty()) first_.pop_back();
    return *this;
}

/*
 * Implementacion: la clave cuenta como inicio del elemento; el valor que la
 * sigue no lleva coma.
 */
JsonWriter& JsonWriter::key(std::string_view name) {
    separate();
    out_ += '"';
    out_ += json_escape(name);
    out_ += "\":";
    after_key_ = true;
    return *this;
}

/*
 * Implementacion: texto escapado entre comillas.
 */
JsonWriter& JsonWriter::value(std::string_view text) {
    separate();
    out_ += '"';
    out_ += json_escape(text);
    out_ += '"';
    return *this;
}

/*
 * Implementacion: to_string ya escribe el signo.
 */
JsonWriter& JsonWriter::value(std::int64_t number) {
    separate();
    out_ += std::to_string(number);
    return *this;
}

/*
 * Implementacion: to_string sin signo.
 */
JsonWriter& JsonWriter::value(std::uint64_t number) {
    separate();
    out_ += std::to_string(number);
    return *this;
}

/*
 * Implementacion: %.15g no agrega ceros de mas (7.415094339622641 se
 * escribe completo; 0.25 se escribe 0.25).
 */
JsonWriter& JsonWriter::value(double number) {
    if (!std::isfinite(number)) return null();
    separate();
    char text[40];
    std::snprintf(text, sizeof text, "%.15g", number);
    out_ += text;
    return *this;
}

/*
 * Implementacion: palabra reservada de JSON.
 */
JsonWriter& JsonWriter::value(bool flag) {
    separate();
    out_ += flag ? "true" : "false";
    return *this;
}

/*
 * Implementacion: palabra reservada de JSON.
 */
JsonWriter& JsonWriter::null() {
    separate();
    out_ += "null";
    return *this;
}

}  // namespace tt::ui

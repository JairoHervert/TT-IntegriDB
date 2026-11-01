// json.hpp
//
// Escritor de JSON minimo para las respuestas de la interfaz. Solo escribe
// (la interfaz nunca recibe JSON: las consultas llegan como texto), por eso
// no hace falta una biblioteca externa.
//
// Uso:
//     JsonWriter w;
//     w.begin_object().key("outcome").value("verified").key("rows").value(9822).end_object();
//     w.str();  // {"outcome":"verified","rows":9822}
//
// Las cadenas se escapan segun RFC 8259: comillas, diagonal invertida y
// caracteres de control. Los bytes >= 0x80 se copian tal cual (se asume
// UTF-8, que es lo que guarda MySQL con utf8mb4).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tt::ui {

/*
 * Escapa un texto para escribirlo entre comillas en JSON.
 *
 * Parametros:
 *   text  texto original
 *
 * Devuelve:
 *   El texto escapado, sin las comillas de los extremos.
 */
std::string json_escape(std::string_view text);

// Escribe un documento JSON de izquierda a derecha. Las comas entre
// elementos se ponen solas; el orden correcto de llamadas (key antes de cada
// valor de un objeto) es responsabilidad de quien lo usa.
class JsonWriter {
public:
    /*
     * Abre un objeto ({).
     */
    JsonWriter& begin_object();

    /*
     * Cierra el objeto abierto (}).
     */
    JsonWriter& end_object();

    /*
     * Abre un arreglo ([).
     */
    JsonWriter& begin_array();

    /*
     * Cierra el arreglo abierto (]).
     */
    JsonWriter& end_array();

    /*
     * Escribe el nombre del siguiente campo de un objeto.
     */
    JsonWriter& key(std::string_view name);

    /*
     * Escribe un texto.
     */
    JsonWriter& value(std::string_view text);

    /*
     * Escribe un texto (evita que un literal se convierta en bool).
     */
    JsonWriter& value(const char* text) { return value(std::string_view(text)); }

    /*
     * Escribe un entero con signo.
     */
    JsonWriter& value(std::int64_t number);

    /*
     * Escribe un entero sin signo (tamanos y contadores).
     */
    JsonWriter& value(std::uint64_t number);

    /*
     * Escribe un entero (atajo para literales y variables int).
     */
    JsonWriter& value(int number) { return value(static_cast<std::int64_t>(number)); }

    /*
     * Escribe un decimal con hasta 15 cifras significativas. NaN e infinito
     * no existen en JSON y se escriben como null.
     */
    JsonWriter& value(double number);

    /*
     * Escribe true o false.
     */
    JsonWriter& value(bool flag);

    /*
     * Escribe null.
     */
    JsonWriter& null();

    /*
     * Documento escrito hasta ahora.
     */
    const std::string& str() const { return out_; }

private:
    /*
     * Pone la coma si el valor que sigue no es el primero de su objeto o
     * arreglo (despues de una clave nunca va coma).
     */
    void separate();

    std::string out_;
    std::vector<bool> first_;  // por nivel abierto: si aun no tiene elementos
    bool after_key_ = false;
};

}  // namespace tt::ui

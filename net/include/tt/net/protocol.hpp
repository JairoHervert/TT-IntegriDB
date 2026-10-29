// protocol.hpp
//
// Protocolo de red entre el modulo verificador (cliente) y el modulo
// probador (servidor). Se usa HTTP/1.1 sobre TCP con dos rutas:
//
//   POST /v1/query    cuerpo: la consulta en texto, igual que en la linea de
//                     comandos (por ejemplo "C4 6 8").
//                     200: el cuerpo es la respuesta (R, pi) en formato
//                          TTRSP (application/octet-stream).
//                     400: la consulta esta mal escrita.
//                     413: la consulta es demasiado larga.
//                     422: la consulta no esta soportada por el estado
//                          autenticado (RNF05).
//                     500: el probador no pudo responder (por ejemplo, MySQL
//                          no responde).
//                     En los errores, el cuerpo es un mensaje en texto.
//
//   GET  /v1/health   estado del probador en texto (ver HealthInfo); sirve
//                     para comprobar la conexion y para el healthcheck de
//                     Docker.
//
// El canal no forma parte de la base de confianza: el verificador revisa la
// respuesta contra delta y la clave publica, sin importar por donde llego.
// Por eso tampoco confia en los mensajes de error del servidor: los muestra
// recortados y sin caracteres de control (safe_text).

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt/crypto/hash.hpp"

namespace tt::net {

// Puerto por omision del probador.
inline constexpr int kDefaultPort = 7070;

// Rutas del protocolo (version 1).
inline constexpr const char* kQueryPath = "/v1/query";
inline constexpr const char* kHealthPath = "/v1/health";

// Tamano maximo del texto de una consulta. La consulta mas larga del
// catalogo ocupa menos de 100 bytes.
inline constexpr std::size_t kMaxQueryBytes = 1024;

// Tamano maximo de una respuesta que acepta el cliente por omision. La
// respuesta mas grande sobre la base pequena (C8, toda la tabla) ocupa unos
// 8 MB; el limite deja margen para bases mayores sin permitir que un
// servidor agote la memoria del cliente.
inline constexpr std::size_t kMaxResponseBytes = std::size_t{256} << 20;

// Direccion de un probador.
struct Endpoint {
    std::string host;
    int port = kDefaultPort;

    /*
     * Texto "host:puerto" para los mensajes.
     */
    std::string text() const { return host + ":" + std::to_string(port); }
};

// Falla de comunicacion: no se pudo abrir el puerto del servidor, conectar
// con el probador o recibir su respuesta completa a tiempo.
class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// La direccion o una opcion de red esta mal escrita.
class AddressError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/*
 * Lee una direccion "host:puerto" o solo "host" (usa kDefaultPort). El host
 * puede ser un nombre (por ejemplo "prover" dentro de Docker) o una IPv4.
 *
 * Parametros:
 *   text  la direccion escrita por el usuario
 *
 * Devuelve:
 *   La direccion leida.
 *
 * Errores:
 *   Lanza AddressError si el host esta vacio o tiene caracteres no validos,
 *   si se escribio un esquema como "http://" o si el puerto no es un entero
 *   entre 1 y 65535.
 */
Endpoint parse_endpoint(const std::string& text);

// Estado que publica el probador en /v1/health.
struct HealthInfo {
    crypto::Digest state_id{};       // id del delta con el que responde
    std::uint64_t state_version = 0;
    std::string plan;                // plan de arboles del estado
    bool public_key = false;         // si cargo la clave publica (C7 a C14)
};

/*
 * Escribe el estado del probador en el formato de /v1/health: una linea de
 * encabezado y una linea "clave valor" por campo.
 *
 * Parametros:
 *   info  estado del probador
 *
 * Devuelve:
 *   El texto, terminado en salto de linea.
 */
std::string health_to_text(const HealthInfo& info);

/*
 * Lee el texto de /v1/health. Como lo envia el servidor, se revisa cada
 * linea y se rechaza cualquier cosa inesperada.
 *
 * Parametros:
 *   text  cuerpo recibido
 *
 * Devuelve:
 *   El estado leido.
 *
 * Errores:
 *   Lanza std::runtime_error si el formato no es el esperado.
 */
HealthInfo health_from_text(const std::string& text);

/*
 * Prepara un texto que viene del otro extremo para mostrarlo: conserva solo
 * caracteres ASCII imprimibles (los demas se cambian por '?') y lo recorta.
 *
 * Parametros:
 *   text       texto recibido
 *   max_chars  longitud maxima del resultado
 *
 * Devuelve:
 *   El texto seguro para imprimir.
 */
std::string safe_text(const std::string& text, std::size_t max_chars = 300);

/*
 * Divide el texto de una consulta en palabras separadas por espacios, como
 * las recibe parse_query.
 *
 * Parametros:
 *   text  cuerpo de la peticion
 *
 * Devuelve:
 *   Las palabras, sin espacios.
 */
std::vector<std::string> split_words(const std::string& text);

/*
 * Une las palabras de una consulta con un espacio, para enviarla.
 *
 * Parametros:
 *   words  palabras de la consulta (sin espacios dentro)
 *
 * Devuelve:
 *   El texto de la consulta.
 */
std::string join_words(const std::vector<std::string>& words);

}  // namespace tt::net

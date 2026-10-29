// commands.hpp
//
// Comandos de tt_verifier. Cada uno lee sus propias opciones, hace su
// trabajo y devuelve el codigo de salida del programa.

#pragma once

#include <string>
#include <vector>

namespace tt::verifier {

// Codigo de salida cuando la respuesta del servidor se rechaza.
constexpr int kExitRejected = 3;

// Codigo de salida cuando el probador no entrega una respuesta: no se pudo
// conectar, se agoto el tiempo o respondio con un error.
constexpr int kExitNoAnswer = 4;

/*
 * Texto de ayuda del comando verify (incluye las formas de consulta que
 * acepta el nucleo).
 */
std::string verify_usage();

/*
 * Texto de ayuda del comando query.
 */
std::string query_usage();

/*
 * Texto de ayuda del comando ping.
 */
std::string ping_usage();

/*
 * Comando verify: Verify de una respuesta guardada en un archivo, para
 * cualquier consulta del catalogo. La revisa contra delta y la clave
 * publica, y muestra el resultado verificado.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "verify"
 *
 * Devuelve:
 *   0 si la respuesta se verifico; kExitRejected si se rechazo; 1 si no se
 *   pudo verificar (archivos faltantes, consulta no soportada, pk que no
 *   corresponde a delta).
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta o las opciones no son validas; deja
 *   pasar las demas excepciones (main las convierte en mensaje y codigo 1).
 */
int run_verify(const std::vector<std::string>& args);

/*
 * Comando query: envia la consulta al probador por la red, recibe la
 * respuesta y la verifica.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "query"
 *
 * Devuelve:
 *   0 si la respuesta se verifico; kExitRejected si se rechazo; 1 si no se
 *   pudo verificar (archivos locales faltantes, consulta no soportada, pk
 *   que no corresponde a delta); kExitNoAnswer si el probador no entrego
 *   respuesta.
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta, la direccion o las opciones no
 *   son validas.
 */
int run_query(const std::vector<std::string>& args);

/*
 * Comando ping: comprueba la conexion con el probador y compara su estado
 * con el delta local.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "ping"
 *
 * Devuelve:
 *   0 si hay conexion y el estado coincide (o no hay delta local), 1 si el
 *   estado no coincide y kExitNoAnswer si no hay conexion.
 *
 * Errores:
 *   Lanza cli::UsageError si las opciones no son validas.
 */
int run_ping(const std::vector<std::string>& args);

}  // namespace tt::verifier

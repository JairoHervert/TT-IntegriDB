// cli.hpp
//
// Utilidades que comparten los comandos de los tres programas (tt_owner,
// tt_prover y tt_verifier): errores de uso, lectura de opciones, medicion
// de tiempos y tamanos de archivo.
//
// Codigos de salida comunes:
//   0  exito
//   1  error al ejecutar el comando
//   2  error de uso (se muestra la ayuda)
//   3  (solo tt_verifier) la respuesta del servidor fue rechazada

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace tt::cli {

using Clock = std::chrono::steady_clock;

/*
 * Error de uso: el usuario escribio mal el comando (opcion desconocida, falta
 * un valor, un valor fuera de rango...). main la atrapa, muestra el mensaje
 * junto con la ayuda y termina con codigo 2, a diferencia de los errores de
 * ejecucion, que terminan con codigo 1.
 */
class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Mide cuanto tiempo ha pasado desde `start`. Sirve para reportar la
 * duracion de cada etapa (RF16).
 *
 * Parametros:
 *   start  instante tomado con Clock::now() al empezar la etapa
 *
 * Devuelve:
 *   Los segundos transcurridos, con fraccion.
 */
double seconds_since(Clock::time_point start);

/*
 * Devuelve el valor de la opcion que esta en args[i] (el argumento que la
 * sigue) y avanza i para no leerlo otra vez como opcion.
 *
 * Errores:
 *   Lanza UsageError si la opcion es el ultimo argumento.
 */
const std::string& option_value(const std::vector<std::string>& args, std::size_t& i);

/*
 * Describe el tamano de un archivo en la unidad mas legible (B, KB o MB).
 */
std::string file_size_text(const std::filesystem::path& path);

/*
 * Describe un tamano en bytes en la unidad mas legible (B, KB o MB).
 */
std::string size_text(std::size_t bytes);

}  // namespace tt::cli

// cli.hpp
//
// Utilidades que comparten los comandos de los tres programas (tt_owner,
// tt_prover y tt_verifier): errores de uso, lectura de opciones, medicion
// de tiempos, tamanos de archivo y la forma de detener un servidor con
// Ctrl+C o SIGTERM.
//
// Codigos de salida comunes:
//   0  exito
//   1  error al ejecutar el comando
//   2  error de uso (se muestra la ayuda)
//   3  (solo tt_verifier) la respuesta del servidor fue rechazada
//   4  (solo tt_verifier) el probador no entrego respuesta (red o error del
//      servidor)

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
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

/*
 * Bloquea SIGINT y SIGTERM en el hilo que la llama. Se llama antes de crear
 * los hilos de un servidor, para que los hereden bloqueadas y solo las
 * reciba el hilo vigilante de run_until_signal.
 */
void block_stop_signals();

/*
 * Ejecuta run() (que bloquea mientras el servidor atiende) y, en un hilo
 * vigilante, espera SIGINT o SIGTERM; al recibir una, avisa y llama a
 * stop(), con lo que run() regresa. Antes se debe haber llamado a
 * block_stop_signals().
 *
 * Parametros:
 *   run   atiende peticiones hasta que se llame stop()
 *   stop  detiene el servidor; se llama desde el hilo vigilante
 *   tag   nombre del programa para el aviso, por ejemplo "prover"
 *
 * Errores:
 *   Deja pasar las excepciones de run(), despues de terminar el vigilante.
 */
void run_until_signal(const std::function<void()>& run, const std::function<void()>& stop, const std::string& tag);

}  // namespace tt::cli

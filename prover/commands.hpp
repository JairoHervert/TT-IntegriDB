// commands.hpp
//
// Comandos de tt_prover. Cada uno lee sus propias opciones, hace su trabajo
// y devuelve el codigo de salida del programa.

#pragma once

#include <string>
#include <vector>

namespace tt::prover {

/*
 * Texto de ayuda del comando answer (incluye las formas de consulta que
 * acepta el nucleo).
 */
std::string answer_usage();

/*
 * Texto de ayuda del comando serve.
 */
std::string serve_usage();

/*
 * Comando answer: ProofGen de una sola consulta del catalogo (C1 a C14 o
 * "range"); guarda la respuesta en un archivo.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "answer"
 *
 * Devuelve:
 *   0 si se genero la respuesta.
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta o las opciones no son validas; deja
 *   pasar las demas excepciones (main las convierte en mensaje y codigo 1).
 */
int run_answer(const std::vector<std::string>& args);

/*
 * Comando serve: ejecuta el probador como servidor de red hasta recibir
 * SIGINT o SIGTERM.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "serve"
 *
 * Devuelve:
 *   0 si el servidor se detuvo normalmente; 1 si no se pudo cargar el
 *   estado o la clave publica no corresponde a delta.
 *
 * Errores:
 *   Lanza cli::UsageError si las opciones no son validas; deja pasar las
 *   demas excepciones (por ejemplo, el puerto ocupado o TT_DB_PASS sin
 *   definir), que main convierte en mensaje y codigo 1.
 */
int run_serve(const std::vector<std::string>& args);

}  // namespace tt::prover

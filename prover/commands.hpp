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
 * Comando answer: ProofGen. Responde una consulta de seleccion o de rango
 * con las filas de MySQL y su prueba, y guarda la respuesta en un archivo.
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

}  // namespace tt::prover

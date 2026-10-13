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

/*
 * Texto de ayuda del comando verify (incluye las formas de consulta que
 * acepta el nucleo).
 */
std::string verify_usage();

/*
 * Comando verify: Verify. Revisa la respuesta del probador a una consulta
 * de seleccion o de rango contra delta y la clave publica, y muestra el
 * resultado verificado.
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

}  // namespace tt::verifier

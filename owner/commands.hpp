// commands.hpp
//
// Comandos de tt_owner. Cada uno lee sus propias opciones, hace su trabajo
// y devuelve el codigo de salida del programa.

#pragma once

#include <string>
#include <vector>

namespace tt::owner {

// Texto de ayuda de cada comando (main los junta en la ayuda general).
extern const char* const kInitUsage;
extern const char* const kSetupUsage;
extern const char* const kCatalogUsage;
extern const char* const kPublishUsage;

/*
 * Comando init: algoritmo Init del esquema. Genera sk_D y pk y las guarda
 * en disco.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "init"
 *
 * Devuelve:
 *   0 si todo salio bien; 1 si ya habia claves (sin --force) o si la
 *   comprobacion de pk fallo.
 *
 * Errores:
 *   Lanza UsageError si las opciones no son validas; deja pasar las demas
 *   excepciones (main las convierte en mensaje y codigo 1).
 */
int run_init(const std::vector<std::string>& args);

/*
 * Comando setup: algoritmo Setup del esquema. Lee la base de MySQL, arma el
 * plan de arboles, construye D~ y delta mostrando el tiempo de cada
 * estructura (RF16) y los guarda en disco.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "setup"
 *
 * Devuelve:
 *   0 si todo salio bien; 1 si ya habia un delta o un D~ (sin --force).
 *
 * Errores:
 *   Lanza UsageError si las opciones no son validas; deja pasar las demas
 *   excepciones.
 */
int run_setup(const std::vector<std::string>& args);

/*
 * Comando catalog: muestra el catalogo de consultas del caso de estudio,
 * los AIT que usa cada una y el plan 'queries' que resulta. Es la
 * justificacion de cada arbol del plan.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "catalog" (no acepta ninguno)
 *
 * Devuelve:
 *   0.
 *
 * Errores:
 *   Lanza UsageError si recibe argumentos.
 */
int run_catalog(const std::vector<std::string>& args);

/*
 * Comando publish: separa en carpetas lo que se entrega a cada modulo
 * (delta, D~ y pk al probador; delta y pk al verificador), despues de
 * comprobar que corresponden al mismo Setup. Nunca copia la clave secreta.
 *
 * Parametros:
 *   args  argumentos que siguen a la palabra "publish"
 *
 * Devuelve:
 *   0 si se copiaron los archivos; 1 si falta alguno o la clave publica no
 *   corresponde a delta.
 *
 * Errores:
 *   Lanza UsageError si las opciones no son validas; deja pasar los errores
 *   de lectura o de consistencia de delta y D~.
 */
int run_publish(const std::vector<std::string>& args);

}  // namespace tt::owner

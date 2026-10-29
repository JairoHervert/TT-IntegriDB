// client_common.hpp
//
// Partes comunes de los comandos del verificador que revisan una respuesta:
// verify (la respuesta esta en un archivo) y query (la respuesta llega por
// la red). Los dos cargan delta y la clave publica de la misma forma,
// revisan que la consulta este soportada y muestran el veredicto con el
// mismo formato; solo cambia de donde salen los bytes.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "tt/query/query.hpp"
#include "tt/query/verifier_service.hpp"
#include "tt/scheme/digest.hpp"
#include "tt/scheme/keys.hpp"

namespace tt::verifier {

// Opciones que comparten verify y query.
struct CommonOptions {
    query::ParsedQuery query;
    std::filesystem::path keys = "keys";              // carpeta con public_key.txt
    std::filesystem::path delta = "setup/delta.txt";  // digest delta vigente
    std::size_t show = 10;                            // filas del resultado que se muestran
    bool aso = false;                                 // comprobacion literal del ASO (C1 a C6)
};

// Lo que el cliente tiene para verificar: delta y la clave publica.
struct ClientState {
    scheme::DatabaseDigest digest;
    scheme::PublicKey public_key;
};

/*
 * Si args[i] es una opcion comun (--keys, --delta, --show, --aso), la lee y
 * avanza i sobre su valor.
 *
 * Parametros:
 *   args  argumentos del comando
 *   i     posicion de la opcion
 *   opt   opciones donde se guarda el valor
 *
 * Devuelve:
 *   true si la opcion era comun.
 *
 * Errores:
 *   Lanza cli::UsageError si le falta su valor o el valor no es valido.
 */
bool parse_common_option(const std::vector<std::string>& args, std::size_t& i, CommonOptions& opt);

/*
 * Lee la consulta a partir de sus palabras.
 *
 * Errores:
 *   Lanza cli::UsageError si la consulta esta mal escrita.
 */
query::ParsedQuery parse_query_words(const std::vector<std::string>& words);

/*
 * Muestra la consulta, carga delta, revisa que la consulta este soportada,
 * carga la clave publica y exige que su huella sea la de delta. Cada paso
 * imprime su resultado o el motivo de la falla.
 *
 * Parametros:
 *   opt    opciones comunes
 *   state  recibe delta y la clave publica
 *
 * Devuelve:
 *   0 si todo esta listo para verificar; 1 si falta un archivo, la consulta
 *   no esta soportada o la clave no corresponde a delta.
 */
int prepare_client(const CommonOptions& opt, ClientState& state);

/*
 * Verifica los bytes recibidos y muestra el veredicto: el resumen, los
 * tiempos por etapa y las primeras filas del resultado.
 *
 * Parametros:
 *   opt    opciones comunes (consulta, filas a mostrar, --aso)
 *   state  delta y clave publica ya cargados
 *   bytes  respuesta del probador
 *
 * Devuelve:
 *   0 si la respuesta se verifico; kExitRejected si se rechazo.
 */
int verify_and_report(const CommonOptions& opt, const ClientState& state, const std::vector<unsigned char>& bytes);

/*
 * Imprime las primeras filas de un resultado como tabla de texto, con el
 * nombre de cada columna como encabezado.
 *
 * Parametros:
 *   table  resultado verificado
 *   limit  numero maximo de filas que se muestran
 */
void print_table(const query::ResultTable& table, std::size_t limit);

/*
 * Texto corto de un digest para los mensajes (16 digitos hexadecimales).
 */
std::string short_id(const crypto::Digest& digest);

}  // namespace tt::verifier

// authenticated_db.hpp
//
// Base de datos autenticada D~: lo que el propietario entrega al servidor
// junto con la base (algoritmo Setup del reporte). Con esto el modulo
// probador genera las pruebas sin conocer ningun secreto.
//
// Contiene:
//   - por cada tabla, el arbol de Merkle de sus filas y los valores de la
//     columna de referencia en el orden de las hojas (para encontrar la
//     posicion de una fila y generar su prueba de inclusion);
//   - cada AIT completo (encabezados, acumuladores, cifrados y valores).
//
// En disco es un archivo binario:
//   "TTADB\0\0\0" | formato (u32) | id del digest delta (32 bytes)
//   tablas: nombre, columnas, referencia, filas (referencia + hash de fila)
//   arboles: tabla, clave, valor, raiz y nodos
//   SHA-256 de todo lo anterior (detecta cualquier dano del archivo)
//
// Al cargarlo se revisa la suma de verificacion (dano accidental), se
// reconstruyen los arboles, se recalculan todos sus hashes y se exige que
// cada raiz coincida con la del digest delta. Asi el servidor detecta a
// tiempo un D~ equivocado o danado.
//
// Dos partes no estan cubiertas por las raices de delta: los valores de las
// hojas de cada AIT (los compromete el acumulador de la hoja, y el
// verificador los revisa con el ASO) y la lista de referencias de cada
// tabla (cada prueba de Merkle usa el hash de la fila completa, que ya
// contiene su referencia). Alguien que edite esas partes y recalcule la
// suma de verificacion obtiene un D~ que carga, pero cuyas respuestas el
// verificador rechaza. La seguridad del esquema nunca depende de este
// archivo: el servidor no es confiable y el verificador revisa cada prueba
// contra delta. Por lo mismo, el probador debera tomar las filas y los
// valores de la base D (o cotejarlos con ella).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

#include "tt/ait/tree.hpp"
#include "tt/db/table.hpp"
#include "tt/merkle/tree.hpp"
#include "tt/scheme/digest.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace tt::scheme {

// Compromiso de las filas de una tabla.
struct AuthenticatedTable {
    db::TableSchema schema;
    std::vector<std::int64_t> references;  // columna de referencia, ascendente; posicion i = hoja i
    merkle::Tree rows;                     // hojas: hashes de fila en ese mismo orden

    /*
     * Posicion de la fila con ese valor en la columna de referencia (la hoja
     * que la representa en el arbol de Merkle).
     *
     * Devuelve:
     *   La posicion, o std::nullopt si no hay fila con ese valor.
     */
    std::optional<std::uint64_t> position_of(std::int64_t reference) const;
};

// Un AIT construido por Setup.
struct AuthenticatedTree {
    TreeSpec spec;
    ait::Tree tree;
};

// D~ completa.
struct AuthenticatedDatabase {
    std::vector<AuthenticatedTable> tables;  // mismo orden que en el digest
    std::vector<AuthenticatedTree> trees;    // mismo orden que en el digest

    /*
     * Busca una tabla por nombre.
     *
     * Devuelve:
     *   Puntero a la tabla, o nullptr si no esta.
     */
    const AuthenticatedTable* find_table(const std::string& table) const;

    /*
     * Busca un AIT.
     *
     * Devuelve:
     *   Puntero al arbol, o nullptr si ese arbol no se construyo.
     */
    const ait::Tree* find_tree(const TreeSpec& spec) const;
};

// Error al leer un archivo D~ (formato, dano o no corresponde al digest).
class AuthenticatedDbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Convierte D~ en los bytes del formato binario descrito arriba.
 *
 * Parametros:
 *   database  la base autenticada
 *   digest    el digest delta del mismo estado (su id queda en el archivo)
 *
 * Devuelve:
 *   El contenido completo del archivo.
 */
std::vector<unsigned char> encode_authenticated_db(const AuthenticatedDatabase& database,
                                                   const DatabaseDigest& digest);

/*
 * Reconstruye D~ a partir de sus bytes y comprueba que corresponda al
 * digest: suma de verificacion del archivo, mismo id, mismas tablas con las
 * mismas raices de Merkle y mismos arboles con las mismas raices (despues
 * de recalcular todos los hashes).
 *
 * Parametros:
 *   data    contenido del archivo
 *   digest  digest delta vigente
 *
 * Devuelve:
 *   La base autenticada lista para generar pruebas.
 *
 * Errores:
 *   Lanza AuthenticatedDbError con el problema encontrado.
 */
AuthenticatedDatabase decode_authenticated_db(const std::vector<unsigned char>& data,
                                              const DatabaseDigest& digest);

/*
 * Guarda D~ en un archivo (escritura atomica).
 */
void save_authenticated_db(const std::filesystem::path& path, const AuthenticatedDatabase& database,
                           const DatabaseDigest& digest);

/*
 * Carga D~ de un archivo con decode_authenticated_db.
 *
 * Errores:
 *   Lanza std::runtime_error si no se puede leer y AuthenticatedDbError
 *   (con la ruta) si el contenido no es valido.
 */
AuthenticatedDatabase load_authenticated_db(const std::filesystem::path& path, const DatabaseDigest& digest);

}  // namespace tt::scheme

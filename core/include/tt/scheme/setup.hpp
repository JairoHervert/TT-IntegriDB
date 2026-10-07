// setup.hpp
//
// Algoritmo Setup del reporte: (D~, delta) <- Setup(D, sk_D).
//
// Para cada tabla calcula el hash de cada fila y el arbol de Merkle sobre
// ellos; para cada arbol del plan construye su AIT con AITSetup. Devuelve
// la base autenticada D~ (para el servidor) y el digest delta (publico).
//
// Antes de construir nada revisa las condiciones que el esquema impone a
// los datos, para fallar con un mensaje claro y no a la mitad:
//   - la base cumple el modelo (db::validate) y el plan es aplicable;
//   - la clave publica corresponde a la clave secreta;
//   - ninguna tabla con arboles tiene mas filas que q (el conjunto mas
//     grande que acumula un AIT es la tabla completa, en la raiz);
//   - ninguna columna usada como valor de un AIT contiene 0 (se acumula
//     v^{-1} y 0 no tiene inverso).
//
// Setup es deterministico: con la misma base y las mismas claves produce
// exactamente el mismo delta (Enc usa IV sintetico y todo lo demas es
// funcion de los datos). Eso permite repetir experimentos (RNF04).

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt/db/table.hpp"
#include "tt/scheme/authenticated_db.hpp"
#include "tt/scheme/digest.hpp"
#include "tt/scheme/keys.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace tt::scheme {

// Aviso de avance que Setup emite al terminar cada estructura, con su
// tiempo (RF16: registrar el tiempo de construccion de las estructuras).
struct SetupProgress {
    enum class Stage { kRows, kTree };
    Stage stage = Stage::kRows;
    std::string table;               // tabla de la estructura
    const TreeSpec* tree = nullptr;  // solo en kTree
    std::size_t position = 0;        // kTree: numero de arbol (desde 1)
    std::size_t total = 0;           // kTree: arboles en el plan
    std::uint64_t items = 0;         // kRows: filas; kTree: hojas del AIT
    std::uint64_t nodes = 0;         // kTree: nodos del AIT
    double seconds = 0;              // tiempo de construccion
};

// Lo que produce Setup.
struct SetupResult {
    AuthenticatedDatabase database;  // D~, para el servidor
    DatabaseDigest digest;           // delta, publico
};

// Los datos o las claves no cumplen alguna condicion de Setup.
class SetupError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Ejecuta Setup.
 *
 * Parametros:
 *   database   la base D
 *   trees      AITs que se construyen (por ejemplo, de plan_trees)
 *   plan_name  nombre del plan, que queda anotado en delta
 *   sk         clave secreta del propietario
 *   pk         clave publica del mismo par (da q y la huella para delta)
 *   progress   funcion opcional que recibe un aviso al terminar cada
 *              estructura (para mostrar tiempos)
 *
 * Devuelve:
 *   D~ y delta, con las tablas y los arboles en orden canonico.
 *
 * Errores:
 *   Lanza SetupError si alguna condicion no se cumple (el mensaje dice cual
 *   y como resolverla).
 */
SetupResult setup(const db::Database& database, const std::vector<TreeSpec>& trees, const std::string& plan_name,
                  const SecretKey& sk, const PublicKey& pk,
                  const std::function<void(const SetupProgress&)>& progress = {});

}  // namespace tt::scheme

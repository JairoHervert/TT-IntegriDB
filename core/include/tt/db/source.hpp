// source.hpp
//
// Puerto de entrada de datos del Propietario: de donde se lee la base D
// para Setup. El nucleo no sabe nada de MySQL; el adaptador real vive en
// la biblioteca tt_mysql (mysql/) y las pruebas usan MemorySource. Asi
// Setup se puede probar sin servidor de base de datos y el codigo
// criptografico no depende de la red.

#pragma once

#include <string>
#include <utility>

#include "tt/db/table.hpp"

namespace tt::db {

// Origen de la base de datos D.
class Source {
public:
    /*
     * Destructor virtual: las fuentes concretas se usan a traves de Source&.
     */
    virtual ~Source() = default;

    /*
     * Lee la base completa (estructura y filas de todas las tablas) en un
     * solo estado consistente.
     *
     * Errores:
     *   Cada adaptador lanza su propio error si no puede leer.
     */
    virtual Database read() = 0;

    /*
     * Describe el origen para los mensajes al usuario, por ejemplo
     * "MySQL academic_db en 127.0.0.1:3306".
     */
    virtual std::string description() const = 0;
};

// Adaptador en memoria: devuelve una base que ya se tiene armada. Lo usan
// las pruebas y cualquier herramienta que genere los datos por su cuenta.
class MemorySource final : public Source {
public:
    /*
     * Guarda la base que se devolvera en read().
     */
    explicit MemorySource(Database database) : database_(std::move(database)) {}

    /*
     * Devuelve una copia de la base guardada.
     */
    Database read() override { return database_; }

    /*
     * Devuelve "base en memoria".
     */
    std::string description() const override { return "base en memoria"; }

private:
    Database database_;
};

}  // namespace tt::db

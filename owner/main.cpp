// main.cpp - Propietario de datos
//
// Responsabilidad (arquitectura logica del reporte): ejecutar Init para
// generar (sk_D, pk) y Setup para construir la base autenticada D~ y el
// digest delta. Entrega pk y delta al verificador, y D~ al servidor.
// Es el unico programa que conoce el secreto s.
//
// Por ahora solo comprueba que el programa enlaza con el nucleo y que el
// grupo bilineal se inicializa correctamente.

#include <cstdio>
#include <exception>

#include "tt/crypto/pairing_group.hpp"

int main() {
    try {
        const tt::crypto::PairingGroup& group = tt::crypto::init_pairing_group();
        if (!group.g1.isValid()) {
            std::fprintf(stderr, "[owner] g1 no es valido\n");
            return 1;
        }
        std::printf("[owner] grupo bilineal BN254 listo\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[owner] error: %s\n", e.what());
        return 1;
    }
    return 0;
}

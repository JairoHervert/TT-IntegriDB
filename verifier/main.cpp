// main.cpp - Cliente SQL + modulo verificador (bloque Cliente)
//
// Responsabilidad (arquitectura logica del reporte): enviar la consulta Q al
// probador, recibir (R, pi) y evaluar Verify(delta, Q, R, pi) con la clave
// publica pk. Entrega R al Cliente SQL junto con el veredicto.
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
            std::fprintf(stderr, "[verifier] g1 no es valido\n");
            return 1;
        }
        std::printf("[verifier] grupo bilineal BN254 listo\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[verifier] error: %s\n", e.what());
        return 1;
    }
    return 0;
}

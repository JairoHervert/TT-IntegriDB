// main.cpp - Modulo probador (bloque Servidor)
//
// Responsabilidad (arquitectura logica del reporte): recibir la consulta Q
// del verificador, obtener del Servidor SQL los resultados intermedios y
// generar el par (R, pi) usando D~ y la clave publica pk. Nunca conoce s.
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
            std::fprintf(stderr, "[prover] g1 no es valido\n");
            return 1;
        }
        std::printf("[prover] grupo bilineal BN254 listo\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[prover] error: %s\n", e.what());
        return 1;
    }
    return 0;
}

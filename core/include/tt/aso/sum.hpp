#pragma once

#include <NTL/ZZ_p.h>
#include <array>
#include <cstdint>
#include <vector>

#include "bn.h"
#include "tt/aso/keys.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/crypto/group_ops.hpp"

namespace tt::aso {
/*
 * Representa la prueba de SUM de IntegriDB (2015), seccion 3.1.
 * P(X) = prod_i (X + grade_i^{-1}) = a0 + a1 X + ... + an X^n.
 * SUM(grade_i) = a1/a0 (mod r). Los testigos certifican a0, a1
 * contra el acumulador del AIT usando pairings, sin conocer s.
 * w1 = g1^((P(s)-a0)/s), w2 = g1^((P(s)-a0-a1*s)/s^2).
 * Los valores se tratan como MULTICONJUNTO: repeticiones cuentan.
 */
struct SumWitness {
    std::array<unsigned char, crypto::kScalarBytes> a0{};
    std::array<unsigned char, crypto::kScalarBytes> a1{};
    bn::Ec1 w1 = crypto::zero_point<bn::Ec1>();
    bn::Ec1 w2 = crypto::zero_point<bn::Ec1>();
};

/*
 * Genera los testigos de SUM para las calificaciones de un alumno usando
 * unicamente las potencias publicas; no utiliza el secreto ASO.
 * Conserva las repeticiones, pues SUM opera sobre un multiconjunto.
 *
 * Parametros:
 *   values  calificaciones de la hoja del AIT (dominio 5..10)
 *   pk      clave publica ASO con potencias suficientes
 *
 * Devuelve:
 *   Los coeficientes a0, a1 y dos testigos de emparejamiento.
 *
 * Errores:
 *   Lanza std::invalid_argument si el conjunto esta vacio, supera
 *   la capacidad publica o contiene calificaciones fuera de 5..10.
 *   Propaga errores de las operaciones criptograficas.
 */
SumWitness prove_sum(const std::vector<std::int64_t>& values, const PublicKey& pk);

/*
 * Verifica una SUM reclamada contra el acumulador autenticado de la hoja
 * del AIT mediante dos identidades de emparejamiento bilineal.
 * Comprueba tambien cotas que excluyen sumas modulares no validas en SQL.
 *
 * Parametros:
 *   authenticated_acc  acumulador G1 autenticado por el AIT
 *   count              cardinalidad autenticada de la hoja
 *   claimed_sum        suma entera declarada por el probador
 *   witness            coeficientes y testigos ASO suministrados
 *   pk                 clave publica ASO
 *
 * Devuelve:
 *   true si las verificaciones se cumplen; false si alguna falla.
 */
bool verify_sum(const bn::Ec1& authenticated_acc, std::uint64_t count,
                std::int64_t claimed_sum, const SumWitness& witness, const PublicKey& pk);
}

// keys.hpp
//
// Claves del bloque ASO (Operaciones Autenticadas sobre Conjuntos) y su
// algoritmo de inicializacion (ASOInit en el reporte).
//
// El acumulador de un conjunto S es acc(S) = g^{prod_{x in S} (x + s)}.
// Quien conoce s lo calcula directamente; quien no lo conoce (el probador)
// lo calcula con las potencias publicas g^{s^i}. Por eso la clave publica
// es una lista de potencias de s "escondidas" en el exponente.

#pragma once

#include <NTL/ZZ_p.h>

#include <cstddef>
#include <vector>

#include "bn.h"

namespace tt::aso {

// Cota maxima aceptada para q: 2^20 = 1,048,576 elementos por conjunto.
// El caso de estudio mas grande tiene ~60,100 filas; el limite solo evita
// reservar cientos de MB por un error al escribir el parametro o por un
// archivo de clave corrupto.
constexpr std::size_t kMaxQ = std::size_t{1} << 20;

// Clave secreta del ASO. Solo la conoce el Propietario de datos.
struct SecretKey {
    NTL::ZZ_p s;  // trampa del acumulador
    NTL::ZZ_p a;  // exponente de extractabilidad (ver PublicKey::g1_as_powers)
};

// Clave publica del ASO (pk_ASO en el reporte).
//
// El reporte la escribe como (g^s, ..., g^{s^q}) con un solo grupo, como el
// articulo de IntegriDB. La implementacion usa un emparejamiento asimetrico
// (G1 x G2 -> GT) y ademas la parte de extractabilidad, igual que
// setup_bilinear() en el codigo original:
//
//   g1_s_powers   el probador calcula acumuladores en G1 (por ejemplo, el
//                 del resultado R de una consulta);
//   g2_s_powers   el probador calcula testigos en G2 (pruebas de
//                 interseccion, digests de los nodos del AIT);
//   g1_as_powers  y g2_a permiten comprobar que un digest enviado por el
//                 servidor se calculo como polinomio en s con la clave
//                 publica: e(g1^{a P(s)}, g2) = e(g1^{P(s)}, g2^a). La
//                 seguridad del ASO de IntegriDB depende de esta suposicion
//                 de extractabilidad (verify_intersection del original).
struct PublicKey {
    std::vector<bn::Ec1> g1_s_powers;   // g1^{s^i},   i = 0..q
    std::vector<bn::Ec2> g2_s_powers;   // g2^{s^i},   i = 0..q
    std::vector<bn::Ec1> g1_as_powers;  // g1^{a s^i}, i = 0..q
    bn::Ec2 g2_a;                       // g2^a

    /*
     * Tamano maximo de un conjunto que se puede acumular con esta clave: el
     * numero de potencias menos uno (un polinomio de grado q tiene q + 1
     * coeficientes). Devuelve 0 si la clave esta vacia.
     */
    std::size_t q() const { return g1_s_powers.empty() ? 0 : g1_s_powers.size() - 1; }
};

// Par de claves que produce init().
struct KeyPair {
    SecretKey sk;
    PublicKey pk;
};

/*
 * ASOInit del reporte: genera la clave secreta (s, a) y la clave publica con
 * todas las potencias necesarias.
 *
 * Parametros:
 *   q  cota superior del tamano de cualquier conjunto que se autenticara;
 *      en el caso de estudio, al menos el numero de filas de la tabla mas
 *      grande.
 *
 * Devuelve:
 *   El par (sk, pk). s y a salen de un generador criptografico y nunca
 *   son cero.
 *
 * Errores:
 *   Lanza std::invalid_argument si q = 0 o q > kMaxQ.
 */
KeyPair init(std::size_t q);

/*
 * Revisa que pk tenga exactamente la estructura que produce init() para
 * algun s y algun a distintos de cero, sin necesitar la clave secreta:
 *   - las tres listas tienen q + 1 puntos, todos en la curva y distintos de O;
 *   - los puntos de G2 estan en el subgrupo de orden r;
 *   - g1_s_powers[0] = g1 y g2_s_powers[0] = g2;
 *   - cada potencia es la anterior multiplicada por el mismo s, las de G1 y
 *     G2 coinciden, y las de extractabilidad usan el mismo a.
 *
 * Las relaciones del ultimo punto se comprueban todas a la vez con una
 * combinacion lineal aleatoria (cinco combinaciones lineales y seis
 * emparejamientos en lugar de miles de emparejamientos): si alguna potencia
 * estuviera mal, la combinacion fallaria salvo con probabilidad de alrededor
 * de 2^-128. Es costosa (del orden de lo que tarda init) y esta pensada para
 * pruebas y para la opcion --check del propietario, no para cada carga de la
 * clave.
 *
 * Parametros:
 *   pk  clave publica a revisar
 *
 * Devuelve:
 *   true si la clave es valida.
 */
bool is_well_formed(const PublicKey& pk);

}  // namespace tt::aso

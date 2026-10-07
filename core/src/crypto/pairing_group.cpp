// pairing_group.cpp

#include "tt/crypto/pairing_group.hpp"

#include <NTL/ZZ.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

namespace tt::crypto {

namespace {

// Orden r de la curva CurveFp254BNb. Es el mismo valor que client.cpp de
// IntegriDB le pasa a ZZ_p::init().
const char* const kGroupOrder =
    "16798108731015832284940804142231733909759579603404752749028378864165570215949";

// Generadores de CurveFp254BNb, tomados de ate-pairing/test/test_point.hpp
// (entrada "Aranha"), igual que en IntegriDB.
const char* const kG2aa =
    "12723517038133731887338407189719511622662176727675373276651903807414909099441";
const char* const kG2ab =
    "4168783608814932154536427934509895782246573715297911553964171371032945126671";
const char* const kG2ba =
    "13891744915211034074451795021214165905772212241412891944830863846330766296736";
const char* const kG2bb =
    "7937318970632701341203597196594272556916396164729705624521405069090520231616";
const int kG1x = -1;
const int kG1y = 1;

/*
 * Inicializa ate-pairing con la curva BN254 y construye los generadores
 * g1 y g2 a partir de sus coordenadas conocidas.
 *
 * Antes de devolverlos comprueba dos cosas: que el orden r que reporta
 * ate-pairing sea el que usaremos como modulo en NTL, y que ambos
 * generadores esten en su curva. Solo la llama init_pairing_group(), una
 * vez por proceso.
 *
 * Devuelve:
 *   Los generadores g1 y g2.
 *
 * Errores:
 *   Lanza std::runtime_error si alguna de las dos comprobaciones falla.
 */
PairingGroup build_group() {
    bn::Param::init(bn::CurveFp254BNb);

    // El modulo de NTL debe ser exactamente el orden r de ate-pairing; si no,
    // los exponentes calculados con NTL no corresponden a los del grupo.
    std::ostringstream r;
    r << bn::Param::r;
    if (r.str() != kGroupOrder) {
        throw std::runtime_error("el orden r de ate-pairing no coincide con el modulo de NTL");
    }

    PairingGroup group{
        bn::Ec1(kG1x, kG1y),
        bn::Ec2(bn::Fp2(bn::Fp(kG2aa), bn::Fp(kG2ab)),
                bn::Fp2(bn::Fp(kG2ba), bn::Fp(kG2bb))),
    };
    if (!group.g1.isValid() || !group.g2.isValid()) {
        throw std::runtime_error("los generadores g1/g2 no estan en la curva");
    }
    return group;
}

}  // namespace

/*
 * Implementacion: el grupo vive en una variable estatica local, que C++
 * construye una sola vez aunque varios hilos llamen al mismo tiempo. El
 * modulo de NTL se fija con una bandera thread_local, porque NTL lo guarda
 * por hilo y un hilo nuevo empieza sin modulo.
 */
const PairingGroup& init_pairing_group() {
    // Inicializacion de variable estatica: C++ garantiza que ocurre una sola
    // vez aunque varios hilos llamen al mismo tiempo.
    static const PairingGroup group = build_group();

    // NTL guarda el modulo de ZZ_p por hilo.
    thread_local bool field_ready = false;
    if (!field_ready) {
        NTL::ZZ_p::init(NTL::conv<NTL::ZZ>(kGroupOrder));
        field_ready = true;
    }
    return group;
}

/*
 * Implementacion: devuelve la constante kGroupOrder.
 */
const char* group_order_decimal() { return kGroupOrder; }

/*
 * Implementacion: llama a opt_atePairing de ate-pairing, que recibe primero
 * el punto de G2 y despues el de G1.
 */
bn::Fp12 pairing(const bn::Ec1& p, const bn::Ec2& q) {
    bn::Fp12 e;
    bn::opt_atePairing(e, q, p);
    return e;
}

/*
 * Implementacion: pasa el valor por su representacion decimal en texto,
 * que es el formato que ambas bibliotecas entienden.
 */
mie::Vuint to_vuint(const NTL::ZZ_p& z) {
    std::ostringstream os;
    os << NTL::rep(z);
    return mie::Vuint(os.str());
}

/*
 * Implementacion: NTL escribe los bytes en orden little-endian; se invierten
 * para obtener big-endian.
 */
std::vector<unsigned char> scalar_to_bytes(const NTL::ZZ_p& z) {
    std::vector<unsigned char> out(kScalarBytes);
    NTL::BytesFromZZ(out.data(), NTL::rep(z), static_cast<long>(out.size()));  // little-endian
    std::reverse(out.begin(), out.end());                                        // big-endian
    return out;
}

/*
 * Implementacion: invierte los bytes para que NTL los lea en little-endian,
 * compara el entero contra r y solo entonces lo convierte a Z_r.
 */
NTL::ZZ_p scalar_from_bytes(const std::vector<unsigned char>& bytes) {
    if (bytes.size() != kScalarBytes) {
        throw std::invalid_argument("un escalar debe ocupar 32 bytes");
    }
    std::vector<unsigned char> little(bytes.rbegin(), bytes.rend());
    NTL::ZZ value;
    NTL::ZZFromBytes(value, little.data(), static_cast<long>(little.size()));
    if (value >= NTL::conv<NTL::ZZ>(kGroupOrder)) {
        throw std::invalid_argument("el escalar no es menor que el orden r");
    }
    init_pairing_group();
    return NTL::conv<NTL::ZZ_p>(value);
}

}  // namespace tt::crypto

// key_io.cpp

#include "tt/scheme/key_io.hpp"

#include <NTL/ZZ.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "tt/crypto/group_ops.hpp"
#include "tt/crypto/pairing_group.hpp"

namespace tt::scheme {

namespace {

const char* const kPublicHeader = "tt-integridb public-key v1";
const char* const kSecretHeader = "tt-integridb secret-key v1";
const char* const kCurveLine = "curve bn254-fp254bnb";

// ---------------------------------------------------------------------------
// Escritura
// ---------------------------------------------------------------------------

/*
 * Escribe un punto de G1 en una linea: sus coordenadas afines x, y en
 * decimal separadas por un espacio, o "0" si es el punto al infinito.
 */
void write_point(std::ostream& out, const bn::Ec1& p) {
    if (p.isZero()) {
        out << "0\n";
        return;
    }
    p.normalize();  // coordenadas afines (z = 1)
    out << p.p[0].toString(10) << ' ' << p.p[1].toString(10) << '\n';
}

/*
 * Escribe un punto de G2 en una linea: las cuatro componentes de sus
 * coordenadas afines (xa xb ya yb) en decimal, o "0" si es el punto al
 * infinito.
 */
void write_point(std::ostream& out, const bn::Ec2& p) {
    if (p.isZero()) {
        out << "0\n";
        return;
    }
    p.normalize();
    out << p.p[0].a_.toString(10) << ' ' << p.p[0].b_.toString(10) << ' '
        << p.p[1].a_.toString(10) << ' ' << p.p[1].b_.toString(10) << '\n';
}

/*
 * Escribe la clave AES como 32 digitos hexadecimales en minusculas.
 */
std::string to_hex(const EncKey& key) {
    static const char* const kDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(key.size() * 2);
    for (unsigned char b : key) {
        hex.push_back(kDigits[b >> 4]);
        hex.push_back(kDigits[b & 0x0f]);
    }
    return hex;
}

/*
 * Arma el texto completo del archivo de clave secreta (encabezado, s, a,
 * clave AES y "end"). Lo usan tanto write_secret_key como save_secret_key;
 * quien lo reciba debe borrarlo de memoria con cleanse() al terminar.
 */
std::string secret_key_text(const SecretKey& sk) {
    std::ostringstream out;
    out << kSecretHeader << '\n'
        << kCurveLine << '\n'
        << "s " << NTL::rep(sk.aso.s) << '\n'
        << "a " << NTL::rep(sk.aso.a) << '\n'
        << "enc_key " << to_hex(sk.enc_key) << '\n'
        << "end\n";
    return out.str();
}

/*
 * Sobrescribe con ceros el contenido de una cadena para que un secreto no
 * quede en memoria despues de usarlo.
 */
void cleanse(std::string& s) {
    if (!s.empty()) OPENSSL_cleanse(&s[0], s.size());
}

// ---------------------------------------------------------------------------
// Lectura
// ---------------------------------------------------------------------------

/*
 * Lee un archivo de clave linea por linea llevando la cuenta del numero de
 * linea, para que cada error diga exactamente donde esta el problema.
 */
class LineReader {
public:
    /*
     * Crea el lector sobre el flujo de entrada.
     */
    explicit LineReader(std::istream& in) : in_(in) {}

    /*
     * Devuelve la siguiente linea sin el fin de linea (acepta el "\r" de
     * Windows). `what` describe lo que se esperaba, para el mensaje de error.
     *
     * Errores:
     *   Lanza KeyFormatError si el archivo ya se termino.
     */
    std::string next(const std::string& what) {
        std::string line;
        if (!std::getline(in_, line)) {
            fail("el archivo termina antes de tiempo; se esperaba " + what);
        }
        ++line_no_;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // fin de linea de Windows
        return line;
    }

    /*
     * Lee la siguiente linea y exige que sea exactamente `expected` (por
     * ejemplo el encabezado o el nombre de una seccion).
     *
     * Errores:
     *   Lanza KeyFormatError si la linea es otra.
     */
    void expect(const std::string& expected) {
        if (next("'" + expected + "'") != expected) fail("se esperaba '" + expected + "'");
    }

    /*
     * Despues de "end" solo se permiten lineas vacias; cualquier otra cosa se
     * rechaza para no aceptar archivos con contenido de mas.
     *
     * Errores:
     *   Lanza KeyFormatError si hay contenido despues de "end".
     */
    void expect_end_of_input() {
        std::string line;
        while (std::getline(in_, line)) {
            ++line_no_;
            if (!line.empty() && line != "\r") fail("hay contenido despues de 'end'");
        }
    }

    /*
     * Lanza KeyFormatError con el numero de la ultima linea leida y el mensaje.
     */
    [[noreturn]] void fail(const std::string& message) const {
        throw KeyFormatError("linea " + std::to_string(line_no_) + ": " + message);
    }

private:
    std::istream& in_;
    long line_no_ = 0;
};

/*
 * Separa una linea en palabras usando los espacios como separador.
 */
std::vector<std::string> split(const std::string& line) {
    std::istringstream is(line);
    std::vector<std::string> tokens;
    std::string token;
    while (is >> token) tokens.push_back(token);
    return tokens;
}

/*
 * Revisa que un texto sea un entero decimal canonico: solo digitos y sin
 * ceros a la izquierda (salvo el propio "0"). Mas de 80 digitos nunca es un
 * elemento valido de estos campos. Esta revision es necesaria antes de pasar
 * el numero a ate-pairing, que termina el proceso ante ciertos formatos.
 */
bool is_canonical_decimal(const std::string& s) {
    if (s.empty() || s.size() > 80) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return s.size() == 1 || s[0] != '0';
}

/*
 * Devuelve el primo p del campo base F_p de la curva (las coordenadas de los
 * puntos deben ser menores que p). Se calcula una sola vez.
 */
const NTL::ZZ& base_field_modulus() {
    static const NTL::ZZ p = [] {
        crypto::init_pairing_group();
        std::ostringstream os;
        os << bn::Param::p;
        return NTL::conv<NTL::ZZ>(os.str().c_str());
    }();
    return p;
}

/*
 * Devuelve el orden r del grupo como entero de NTL (los escalares secretos
 * deben ser menores que r). Se calcula una sola vez.
 */
const NTL::ZZ& group_order() {
    static const NTL::ZZ r = NTL::conv<NTL::ZZ>(crypto::group_order_decimal());
    return r;
}

/*
 * Convierte el texto de una coordenada en un elemento de F_p. Exige forma
 * canonica y valor menor que p, para que cada punto tenga una sola
 * representacion valida en el archivo.
 *
 * Errores:
 *   Lanza KeyFormatError (via reader.fail) si el texto no es valido.
 */
bn::Fp parse_coordinate(const LineReader& reader, const std::string& token) {
    if (!is_canonical_decimal(token)) {
        reader.fail("coordenada invalida '" + token + "' (se espera un entero decimal)");
    }
    if (NTL::conv<NTL::ZZ>(token.c_str()) >= base_field_modulus()) {
        reader.fail("coordenada fuera del campo (mayor o igual que p)");
    }
    bn::Fp x;
    x.set(token);
    return x;
}

/*
 * Convierte una linea "x y" en un punto de G1 y revisa que este en la curva.
 * Rechaza el punto al infinito, que nunca aparece en una clave valida.
 *
 * Errores:
 *   Lanza KeyFormatError si la linea no describe un punto valido.
 */
bn::Ec1 parse_g1(const LineReader& reader, const std::string& line) {
    const std::vector<std::string> t = split(line);
    if (t.size() == 1 && t[0] == "0") reader.fail("una clave no puede contener el punto al infinito");
    if (t.size() != 2) reader.fail("un punto de G1 lleva 2 coordenadas");
    bn::Ec1 p;
    p.set(parse_coordinate(reader, t[0]), parse_coordinate(reader, t[1]), false);
    if (!p.isValid()) reader.fail("el punto no esta en la curva");
    return p;
}

/*
 * Convierte una linea "xa xb ya yb" en un punto de G2 y revisa que este en
 * la curva torcida. Rechaza el punto al infinito.
 *
 * Errores:
 *   Lanza KeyFormatError si la linea no describe un punto valido.
 */
bn::Ec2 parse_g2(const LineReader& reader, const std::string& line) {
    const std::vector<std::string> t = split(line);
    if (t.size() == 1 && t[0] == "0") reader.fail("una clave no puede contener el punto al infinito");
    if (t.size() != 4) reader.fail("un punto de G2 lleva 4 coordenadas");
    bn::Ec2 p;
    p.set(bn::Fp2(parse_coordinate(reader, t[0]), parse_coordinate(reader, t[1])),
          bn::Fp2(parse_coordinate(reader, t[2]), parse_coordinate(reader, t[3])), false);
    if (!p.isValid()) reader.fail("el punto no esta en la curva torcida");
    return p;
}

/*
 * Lee una seccion de la clave publica: la linea con su nombre seguida de
 * `count` puntos, cada uno convertido con `parse` (parse_g1 o parse_g2).
 *
 * Errores:
 *   Lanza KeyFormatError si falta la seccion o algun punto no es valido.
 */
template <class Point, class Parser>
void read_points(LineReader& reader, const std::string& section, std::size_t count,
                 std::vector<Point>& out, Parser parse) {
    reader.expect(section);
    // Sin reserve(): si el archivo esta truncado, falla antes de reservar
    // memoria para todos los puntos que anuncia q.
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(parse(reader, reader.next("un punto de " + section)));
    }
}

/*
 * Lee una linea "<nombre> <decimal>" de la clave secreta (s o a) y exige que
 * el valor este entre 1 y r - 1.
 *
 * Errores:
 *   Lanza KeyFormatError si el nombre o el valor no son validos.
 */
NTL::ZZ_p parse_secret_scalar(LineReader& reader, const std::string& name) {
    const std::vector<std::string> t = split(reader.next("'" + name + " <entero>'"));
    if (t.size() != 2 || t[0] != name || !is_canonical_decimal(t[1])) {
        reader.fail("se esperaba '" + name + " <entero decimal>'");
    }
    const NTL::ZZ value = NTL::conv<NTL::ZZ>(t[1].c_str());
    if (NTL::IsZero(value) || value >= group_order()) {
        reader.fail(name + " debe estar entre 1 y r - 1");
    }
    crypto::init_pairing_group();
    return NTL::conv<NTL::ZZ_p>(value);
}

/*
 * Valor (0 a 15) de un digito hexadecimal, o -1 si el caracter no lo es.
 */
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * Lee la linea "enc_key <32 digitos hex>" y la convierte en los 16 bytes de
 * la clave AES.
 *
 * Errores:
 *   Lanza KeyFormatError si la longitud o los caracteres no son validos.
 */
EncKey parse_enc_key(LineReader& reader) {
    const std::vector<std::string> t = split(reader.next("'enc_key <hex>'"));
    if (t.size() != 2 || t[0] != "enc_key" || t[1].size() != 2 * kEncKeyBytes) {
        reader.fail("se esperaba 'enc_key' seguido de " + std::to_string(2 * kEncKeyBytes) +
                    " digitos hexadecimales");
    }
    EncKey key{};
    for (std::size_t i = 0; i < kEncKeyBytes; ++i) {
        const int hi = hex_value(t[1][2 * i]);
        const int lo = hex_value(t[1][2 * i + 1]);
        if (hi < 0 || lo < 0) reader.fail("enc_key tiene caracteres que no son hexadecimales");
        key[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return key;
}

/*
 * Lanza std::system_error con el codigo de error del sistema actual (errno)
 * y la descripcion `what`.
 */
[[noreturn]] void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace

// ---------------------------------------------------------------------------
// Flujos
// ---------------------------------------------------------------------------

/*
 * Implementacion: encabezado, curva y q; despues las tres listas de
 * potencias y g2^a, cada una con su nombre de seccion, y "end".
 */
void write_public_key(std::ostream& out, const PublicKey& pk) {
    const aso::PublicKey& k = pk.aso;
    out << kPublicHeader << '\n' << kCurveLine << '\n' << "q " << k.q() << '\n';
    out << "g1_s_powers\n";
    for (const bn::Ec1& p : k.g1_s_powers) write_point(out, p);
    out << "g2_s_powers\n";
    for (const bn::Ec2& p : k.g2_s_powers) write_point(out, p);
    out << "g1_as_powers\n";
    for (const bn::Ec1& p : k.g1_as_powers) write_point(out, p);
    out << "g2_a\n";
    write_point(out, k.g2_a);
    out << "end\n";
}

/*
 * Implementacion: lee y valida el encabezado y q, lee las tres secciones de
 * puntos y g2^a, exige "end" y al final hace las revisiones de generadores y
 * de subgrupo.
 */
PublicKey read_public_key(std::istream& in) {
    const crypto::PairingGroup& group = crypto::init_pairing_group();
    LineReader reader(in);

    reader.expect(kPublicHeader);
    reader.expect(kCurveLine);

    const std::vector<std::string> t = split(reader.next("'q <entero>'"));
    if (t.size() != 2 || t[0] != "q" || !is_canonical_decimal(t[1]) || t[1].size() > 9) {
        reader.fail("se esperaba 'q <entero>'");
    }
    const std::size_t q = static_cast<std::size_t>(std::stoul(t[1]));
    if (q == 0 || q > aso::kMaxQ) {
        reader.fail("q debe estar entre 1 y " + std::to_string(aso::kMaxQ));
    }

    PublicKey pk;
    aso::PublicKey& k = pk.aso;
    read_points(reader, "g1_s_powers", q + 1, k.g1_s_powers, parse_g1);
    read_points(reader, "g2_s_powers", q + 1, k.g2_s_powers, parse_g2);
    read_points(reader, "g1_as_powers", q + 1, k.g1_as_powers, parse_g1);
    reader.expect("g2_a");
    k.g2_a = parse_g2(reader, reader.next("el punto g2_a"));
    reader.expect("end");
    reader.expect_end_of_input();

    if (!(k.g1_s_powers[0] == group.g1) || !(k.g2_s_powers[0] == group.g2)) {
        throw KeyFormatError("la potencia 0 de la clave publica no es el generador del grupo");
    }
    if (!crypto::in_prime_order_subgroup(k.g2_s_powers[1]) ||
        !crypto::in_prime_order_subgroup(k.g2_a)) {
        throw KeyFormatError("g2^s o g2^a no estan en el subgrupo de orden r");
    }
    return pk;
}

/*
 * Implementacion: arma el texto con secret_key_text, lo escribe y lo borra
 * de memoria.
 */
void write_secret_key(std::ostream& out, const SecretKey& sk) {
    std::string text = secret_key_text(sk);
    out << text;
    cleanse(text);
}

/*
 * Implementacion: valida encabezado y curva, lee s, a y la clave AES en ese
 * orden y exige "end".
 */
SecretKey read_secret_key(std::istream& in) {
    LineReader reader(in);
    reader.expect(kSecretHeader);
    reader.expect(kCurveLine);

    SecretKey sk;
    sk.aso.s = parse_secret_scalar(reader, "s");
    sk.aso.a = parse_secret_scalar(reader, "a");
    sk.enc_key = parse_enc_key(reader);
    reader.expect("end");
    reader.expect_end_of_input();
    return sk;
}

// ---------------------------------------------------------------------------
// Archivos
// ---------------------------------------------------------------------------

/*
 * Implementacion: escribe en <ruta>.tmp y lo renombra (el renombrado es
 * atomico en Linux). Ante cualquier error borra el temporal y relanza.
 */
void save_public_key(const std::filesystem::path& path, const PublicKey& pk) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    try {
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("no se pudo crear " + tmp.string());
            write_public_key(out, pk);
            out.flush();
            if (!out) throw std::runtime_error("error al escribir " + tmp.string());
        }
        std::filesystem::rename(tmp, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        throw;
    }
}

/*
 * Implementacion: abre el archivo y usa read_public_key; si hay error de
 * formato, agrega la ruta al mensaje.
 */
PublicKey load_public_key(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("no se pudo abrir " + path.string());
    try {
        return read_public_key(in);
    } catch (const KeyFormatError& e) {
        throw KeyFormatError(path.string() + ": " + e.what());
    }
}

/*
 * Implementacion: usa open() de POSIX para crear el archivo ya con permisos
 * 0600 (con O_EXCL si no se permite sobrescribir), fuerza los permisos con
 * fchmod, escribe todo el texto reintentando si write() se interrumpe, lo
 * borra de memoria y hace fsync para que quede en disco.
 */
void save_secret_key(const std::filesystem::path& path, const SecretKey& sk,
                     Overwrite overwrite) {
    int flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
    if (overwrite == Overwrite::kNo) flags |= O_EXCL;

    const int fd = ::open(path.c_str(), flags, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (errno == EEXIST) {
            throw std::runtime_error(path.string() + " ya existe; no se sobrescribe una clave secreta");
        }
        throw_errno("no se pudo crear " + path.string());
    }
    // Si el archivo ya existia (Overwrite::kYes), open() no cambia sus
    // permisos; se fuerzan a 0600.
    if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        const int err = errno;
        ::close(fd);
        throw std::system_error(err, std::generic_category(), "no se pudieron fijar permisos de " + path.string());
    }

    std::string text = secret_key_text(sk);
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            const int err = errno;
            cleanse(text);
            ::close(fd);
            ::unlink(path.c_str());  // no dejar una clave a medio escribir
            throw std::system_error(err, std::generic_category(), "error al escribir " + path.string());
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    cleanse(text);

    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        throw_errno("error al cerrar " + path.string());
    }
}

/*
 * Implementacion: abre el archivo y usa read_secret_key; si hay error de
 * formato, agrega la ruta al mensaje.
 */
SecretKey load_secret_key(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("no se pudo abrir " + path.string());
    try {
        return read_secret_key(in);
    } catch (const KeyFormatError& e) {
        throw KeyFormatError(path.string() + ": " + e.what());
    }
}

}  // namespace tt::scheme

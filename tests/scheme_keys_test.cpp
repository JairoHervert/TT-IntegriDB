// scheme_keys_test.cpp
//
// Pruebas del Init del esquema y de los archivos de claves.

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "tt/aso/keys.hpp"
#include "tt/crypto/pairing_group.hpp"
#include "tt/scheme/key_io.hpp"
#include "tt/scheme/keys.hpp"

namespace {

namespace fs = std::filesystem;
using tt::scheme::KeyFormatError;

/*
 * Indica si dos claves publicas son identicas: mismas potencias de s en G1
 * y G2, mismas potencias de extractabilidad y mismo g2^a. Se usa para
 * comprobar que escribir y volver a leer una clave no la altera.
 */
bool same_public_key(const tt::scheme::PublicKey& x, const tt::scheme::PublicKey& y) {
    const tt::aso::PublicKey& a = x.aso;
    const tt::aso::PublicKey& b = y.aso;
    return a.g1_s_powers == b.g1_s_powers && a.g2_s_powers == b.g2_s_powers &&
           a.g1_as_powers == b.g1_as_powers && a.g2_a == b.g2_a;
}

/*
 * Indica si dos claves secretas son identicas: mismos s, a y clave de Enc.
 */
bool same_secret_key(const tt::scheme::SecretKey& x, const tt::scheme::SecretKey& y) {
    return x.aso.s == y.aso.s && x.aso.a == y.aso.a && x.enc_key == y.enc_key;
}

/*
 * Devuelve el texto que se escribiria en public_key.txt para `pk`, sin
 * tocar el disco. Sirve como punto de partida para las pruebas que alteran
 * el archivo.
 */
std::string public_text(const tt::scheme::PublicKey& pk) {
    std::ostringstream out;
    tt::scheme::write_public_key(out, pk);
    return out.str();
}

/*
 * Devuelve el texto que se escribiria en secret_key.txt para `sk`, sin
 * tocar el disco.
 */
std::string secret_text(const tt::scheme::SecretKey& sk) {
    std::ostringstream out;
    tt::scheme::write_secret_key(out, sk);
    return out.str();
}

/*
 * Lee una clave publica desde un texto en memoria (como si fuera el
 * contenido de public_key.txt). Lanza KeyFormatError si el texto no es
 * valido.
 */
tt::scheme::PublicKey read_public(const std::string& text) {
    std::istringstream in(text);
    return tt::scheme::read_public_key(in);
}

/*
 * Lee una clave secreta desde un texto en memoria (como si fuera el
 * contenido de secret_key.txt). Lanza KeyFormatError si el texto no es
 * valido.
 */
tt::scheme::SecretKey read_secret(const std::string& text) {
    std::istringstream in(text);
    return tt::scheme::read_secret_key(in);
}

/*
 * Parte un texto en lineas (sin el salto de linea), para poder editar
 * lineas sueltas de un archivo de claves.
 */
std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

/*
 * Une las lineas en un solo texto, cada una terminada en salto de linea.
 * Es la operacion inversa de lines_of.
 */
std::string join(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) text += line + "\n";
    return text;
}

/*
 * Altera un archivo de clave publica y comprueba que la lectura lo
 * rechace con KeyFormatError (y no, por ejemplo, terminando el proceso
 * como hace el lector de ate-pairing con algunos numeros mal escritos).
 *
 * Parametros:
 *   text  contenido valido de public_key.txt
 *   edit  funcion que modifica las lineas (quita una, cambia un numero...)
 *   name  descripcion de la alteracion, para la salida de la prueba
 */
void expect_public_rejected(const std::string& text,
                            const std::function<void(std::vector<std::string>&)>& edit,
                            const std::string& name) {
    std::vector<std::string> lines = lines_of(text);
    edit(lines);
    const std::string edited = join(lines);
    tt::test::check_throws<KeyFormatError>([&] { read_public(edited); }, name);
}

/*
 * Igual que expect_public_rejected, pero para secret_key.txt.
 *
 * Parametros:
 *   text  contenido valido de secret_key.txt
 *   edit  funcion que modifica las lineas
 *   name  descripcion de la alteracion, para la salida de la prueba
 */
void expect_secret_rejected(const std::string& text,
                            const std::function<void(std::vector<std::string>&)>& edit,
                            const std::string& name) {
    std::vector<std::string> lines = lines_of(text);
    edit(lines);
    const std::string edited = join(lines);
    tt::test::check_throws<KeyFormatError>([&] { read_secret(edited); }, name);
}

/*
 * Devuelve la posicion de la primera linea igual a `value` (por ejemplo, el
 * encabezado de una seccion), o lines.size() si no esta.
 */
std::size_t index_of(const std::vector<std::string>& lines, const std::string& value) {
    return static_cast<std::size_t>(std::find(lines.begin(), lines.end(), value) - lines.begin());
}

}  // namespace

/*
 * Pruebas del Init del esquema y de los archivos de claves: Init produce
 * claves validas y distintas en cada ejecucion; escribir y leer una clave
 * la deja igual; la lectura rechaza archivos alterados, incompletos o con
 * basura; y los archivos se crean con los permisos esperados sin
 * sobrescribir claves existentes por accidente.
 *
 * Devuelve:
 *   0 si todas las comprobaciones pasan; 1 si alguna falla.
 */
int main() {
    using tt::test::check;
    using tt::test::check_throws;
    using tt::test::section;

    const tt::crypto::PairingGroup& group = tt::crypto::init_pairing_group();
    const std::size_t q = 8;

    section("Init del esquema");
    const tt::scheme::KeyPair keys = tt::scheme::init(q);
    check(keys.pk.aso.q() == q, "pk admite conjuntos de hasta q elementos");
    check(tt::aso::is_well_formed(keys.pk.aso), "pk tiene la estructura correcta");
    check(std::any_of(keys.sk.enc_key.begin(), keys.sk.enc_key.end(),
                      [](unsigned char b) { return b != 0; }),
          "la clave AES no es todo ceros");
    const tt::scheme::KeyPair keys2 = tt::scheme::init(q);
    check(keys.sk.enc_key != keys2.sk.enc_key && keys.sk.aso.s != keys2.sk.aso.s,
          "dos ejecuciones de Init dan claves distintas");

    section("Clave publica: escribir y leer");
    const std::string pk_text = public_text(keys.pk);
    {
        const tt::scheme::PublicKey loaded = read_public(pk_text);
        check(same_public_key(loaded, keys.pk), "leer lo escrito devuelve la misma clave");
        check(public_text(loaded) == pk_text, "volver a escribirla da exactamente el mismo texto");
        check(tt::aso::is_well_formed(loaded.aso), "la clave leida tiene la estructura correcta");
    }
    {
        std::string crlf;
        for (const std::string& line : lines_of(pk_text)) crlf += line + "\r\n";
        check(same_public_key(read_public(crlf), keys.pk),
              "acepta fines de linea de Windows (CRLF)");
    }

    section("Clave publica: rechazos");
    const std::size_t first_g1 = index_of(lines_of(pk_text), "g1_s_powers") + 1;
    const std::size_t first_g2 = index_of(lines_of(pk_text), "g2_s_powers") + 1;
    expect_public_rejected(pk_text, [](auto& l) { l[0] = "tt-integridb public-key v2"; },
                           "encabezado de otra version");
    expect_public_rejected(pk_text, [](auto& l) { l[1] = "curve bls12-381"; }, "otra curva");
    expect_public_rejected(pk_text, [](auto& l) { l[2] = "q 0"; }, "q = 0");
    expect_public_rejected(pk_text, [](auto& l) { l[2] = "q 9"; },
                           "q no coincide con el numero de puntos");
    expect_public_rejected(pk_text, [](auto& l) { l.resize(l.size() / 2); }, "archivo truncado");
    expect_public_rejected(pk_text, [](auto& l) { l.pop_back(); }, "falta 'end'");
    expect_public_rejected(pk_text, [](auto& l) { l.push_back("basura"); },
                           "contenido despues de 'end'");
    expect_public_rejected(pk_text, [&](auto& l) { l[first_g1 + 1] = "0" + l[first_g1 + 1]; },
                           "coordenada con cero a la izquierda (ate-pairing haria exit(1))");
    expect_public_rejected(pk_text, [&](auto& l) { l[first_g1 + 1] = "12abc 34"; },
                           "coordenada que no es numero");
    expect_public_rejected(
        pk_text,
        [&](auto& l) {
            std::ostringstream p;
            p << bn::Param::p;
            l[first_g1 + 1] = p.str() + " 1";
        },
        "coordenada mayor o igual que p");
    expect_public_rejected(pk_text,
                           [&](auto& l) {
                               std::string& line = l[first_g1 + 1];
                               line.back() = line.back() == '1' ? '2' : '1';
                           },
                           "punto de G1 fuera de la curva");
    expect_public_rejected(pk_text,
                           [&](auto& l) {
                               std::string& line = l[first_g2 + 2];
                               line.back() = line.back() == '1' ? '2' : '1';
                           },
                           "punto de G2 fuera de la curva torcida");
    expect_public_rejected(pk_text, [&](auto& l) { l[first_g1 + 1] = "0"; },
                           "punto al infinito dentro de la clave");
    expect_public_rejected(pk_text, [&](auto& l) { l[first_g1 + 1] = "1 2 3"; },
                           "punto de G1 con 3 coordenadas");
    expect_public_rejected(pk_text, [&](auto& l) { l[first_g1] = l[first_g1 + 1]; },
                           "la potencia 0 no es el generador");

    section("Clave secreta: escribir y leer");
    const std::string sk_text = secret_text(keys.sk);
    check(same_secret_key(read_secret(sk_text), keys.sk), "leer lo escrito devuelve la misma clave");
    expect_secret_rejected(sk_text, [](auto& l) { l[2] = "s 0"; }, "s = 0");
    expect_secret_rejected(sk_text,
                           [](auto& l) { l[2] = std::string("s ") + tt::crypto::group_order_decimal(); },
                           "s = r (fuera de Z_r)");
    expect_secret_rejected(sk_text, [](auto& l) { l[3] = "a 007"; }, "a con ceros a la izquierda");
    expect_secret_rejected(sk_text, [](auto& l) { l[4] = "enc_key 00ff"; }, "clave AES corta");
    expect_secret_rejected(sk_text,
                           [](auto& l) { l[4] = "enc_key " + std::string(32, 'g'); },
                           "clave AES con caracteres no hexadecimales");
    expect_secret_rejected(sk_text, [](auto& l) { l.erase(l.begin() + 3); }, "falta la linea de a");

    section("Archivos");
    const fs::path dir = fs::temp_directory_path() / ("tt_keys_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        const fs::path pk_path = dir / "public_key.txt";
        tt::scheme::save_public_key(pk_path, keys.pk);
        check(same_public_key(tt::scheme::load_public_key(pk_path), keys.pk),
              "guardar y cargar la clave publica");
        check(!fs::exists(dir / "public_key.txt.tmp"), "no queda el archivo temporal");

        const fs::path sk_path = dir / "secret_key.txt";
        tt::scheme::save_secret_key(sk_path, keys.sk);
        const fs::perms perms = fs::status(sk_path).permissions() & fs::perms::all;
        check(perms == (fs::perms::owner_read | fs::perms::owner_write),
              "la clave secreta se crea con permisos 600");
        check(same_secret_key(tt::scheme::load_secret_key(sk_path), keys.sk),
              "guardar y cargar la clave secreta");

        check_throws<std::runtime_error>(
            [&] { tt::scheme::save_secret_key(sk_path, keys2.sk); },
            "no sobrescribe una clave secreta existente");
        check(same_secret_key(tt::scheme::load_secret_key(sk_path), keys.sk),
              "despues del intento, la clave original sigue intacta");

        fs::permissions(sk_path, fs::perms::owner_read | fs::perms::owner_write |
                                     fs::perms::group_read | fs::perms::others_read);
        tt::scheme::save_secret_key(sk_path, keys2.sk, tt::scheme::Overwrite::kYes);
        check(same_secret_key(tt::scheme::load_secret_key(sk_path), keys2.sk) &&
                  (fs::status(sk_path).permissions() & fs::perms::all) ==
                      (fs::perms::owner_read | fs::perms::owner_write),
              "con Overwrite::kYes la reemplaza y vuelve a dejar permisos 600");

        check_throws<std::runtime_error>(
            [&] { tt::scheme::load_public_key(dir / "no_existe.txt"); },
            "cargar un archivo que no existe lanza un error");
    }
    fs::remove_all(dir);
    (void)group;

    return tt::test::finish();
}

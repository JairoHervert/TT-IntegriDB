// connection.cpp

#include "tt/mysql/connection.hpp"

#include <mysql/mysql.h>

#include <cstdlib>
#include <string>

namespace tt::mysql {

namespace {

/*
 * Devuelve el valor de una variable de entorno, o `fallback` si no esta
 * definida o esta vacia.
 */
std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

}  // namespace

// Estado interno: el manejador de la conexion de libmysqlclient.
struct Connection::Impl {
    MYSQL* handle = nullptr;

    /*
     * Lanza DatabaseError con el contexto, el codigo y el mensaje del
     * servidor para el ultimo error de esta conexion.
     */
    [[noreturn]] void fail(const std::string& context) const {
        throw DatabaseError(context + ": [" + std::to_string(mysql_errno(handle)) + "] " + mysql_error(handle));
    }
};

/*
 * Implementacion: toma cada variable o su valor por defecto; el puerto
 * debe ser un numero de 1 a 65535 y la contrasena debe estar definida
 * (puede ser vacia).
 */
ConnectionConfig ConnectionConfig::from_environment() {
    ConnectionConfig c;
    c.host = env_or("TT_DB_HOST", c.host.c_str());
    c.user = env_or("TT_DB_USER", c.user.c_str());
    c.database = env_or("TT_DB_NAME", c.database.c_str());
    const std::string port = env_or("TT_DB_PORT", "3306");
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos ||
        std::stoul(port) == 0 || std::stoul(port) > 65535) {
        throw ConfigError("TT_DB_PORT debe ser un puerto entre 1 y 65535, se recibio '" + port + "'");
    }
    c.port = static_cast<unsigned int>(std::stoul(port));
    const char* password = std::getenv("TT_DB_PASS");
    if (password == nullptr) {
        throw ConfigError("falta la variable de entorno TT_DB_PASS con la contrasena de MySQL "
                          "(por ejemplo: export TT_DB_PASS=app_pass_2026)");
    }
    c.password = password;
    return c;
}

/*
 * Implementacion: arma el texto con base, host, puerto y usuario.
 */
std::string ConnectionConfig::describe() const {
    return "MySQL " + database + " en " + host + ":" + std::to_string(port) + " (usuario " + user + ")";
}

/*
 * Implementacion: mysql_init, opciones (utf8mb4, TCP, 5 s para conectar) y
 * mysql_real_connect. Si algo falla, libera el manejador antes de lanzar.
 */
Connection::Connection(const ConnectionConfig& config) : impl_(std::make_unique<Impl>()) {
    impl_->handle = mysql_init(nullptr);
    if (impl_->handle == nullptr) throw DatabaseError("mysql_init fallo (sin memoria)");
    unsigned int protocol = MYSQL_PROTOCOL_TCP;
    unsigned int timeout = 5;
    mysql_options(impl_->handle, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    mysql_options(impl_->handle, MYSQL_OPT_PROTOCOL, &protocol);
    mysql_options(impl_->handle, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    if (mysql_real_connect(impl_->handle, config.host.c_str(), config.user.c_str(), config.password.c_str(),
                           config.database.c_str(), config.port, nullptr, 0) == nullptr) {
        const std::string message = "no se pudo conectar a " + config.describe() + ": [" +
                                    std::to_string(mysql_errno(impl_->handle)) + "] " + mysql_error(impl_->handle);
        mysql_close(impl_->handle);
        impl_->handle = nullptr;
        throw DatabaseError(message);
    }
}

/*
 * Implementacion: mysql_close libera todo lo asociado a la conexion.
 */
Connection::~Connection() {
    if (impl_ && impl_->handle != nullptr) mysql_close(impl_->handle);
}

/*
 * Implementacion: mysql_real_query; si la instruccion devolviera filas, se
 * descartan para dejar la conexion lista.
 */
void Connection::execute(const std::string& sql) {
    if (mysql_real_query(impl_->handle, sql.data(), sql.size()) != 0) impl_->fail("error en '" + sql + "'");
    MYSQL_RES* res = mysql_store_result(impl_->handle);
    if (res != nullptr) mysql_free_result(res);
}

/*
 * Implementacion: mysql_real_query y mysql_use_result (las filas llegan una
 * por una); cada campo se copia con su longitud real (mysql_fetch_lengths),
 * para no cortar textos que contengan bytes cero. Al terminar revisa si la
 * lectura se interrumpio por un error. El resultado se libera aunque
 * on_row lance una excepcion.
 */
void Connection::query(const std::string& sql, const std::function<void(const std::vector<Field>&)>& on_row) {
    if (mysql_real_query(impl_->handle, sql.data(), sql.size()) != 0) impl_->fail("error en la consulta");
    MYSQL_RES* res = mysql_use_result(impl_->handle);
    if (res == nullptr) impl_->fail("la consulta no devolvio resultado");
    const unsigned int columns = mysql_num_fields(res);
    std::vector<Field> fields(columns);
    try {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            const unsigned long* lengths = mysql_fetch_lengths(res);
            for (unsigned int i = 0; i < columns; ++i) {
                fields[i] = row[i] == nullptr ? Field{} : Field{std::string(row[i], lengths[i])};
            }
            on_row(fields);
        }
        if (mysql_errno(impl_->handle) != 0) impl_->fail("error al leer el resultado");
    } catch (...) {
        while (mysql_fetch_row(res) != nullptr) {
        }  // vaciar lo pendiente para dejar la conexion usable
        mysql_free_result(res);
        throw;
    }
    mysql_free_result(res);
}

/*
 * Implementacion: mysql_get_server_info.
 */
std::string Connection::server_version() const { return mysql_get_server_info(impl_->handle); }

}  // namespace tt::mysql

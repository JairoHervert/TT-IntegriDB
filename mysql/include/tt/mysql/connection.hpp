// connection.hpp
//
// Conexion a MySQL con la API en C de libmysqlclient (la misma que usa
// IntegriDB y que se probo en TT_auxiliares/prueba-mysql-docker). Envuelve
// el MYSQL* para que se cierre solo y para que cada error llegue como
// excepcion con el mensaje del servidor.
//
// Los datos de conexion salen de variables de entorno, con los valores de
// docker-compose.yml por defecto:
//   TT_DB_HOST (127.0.0.1)  TT_DB_PORT (3306)  TT_DB_USER (app_user)
//   TT_DB_NAME (academic_db)  TT_DB_PASS (obligatoria, sin valor por defecto)
// La contrasena no tiene valor por defecto para no dejarla escrita en el
// codigo; se define con:  export TT_DB_PASS=...
//
// El host debe ser "127.0.0.1" y no "localhost": con "localhost" la
// biblioteca intenta el socket Unix, que no existe fuera del contenedor.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tt::mysql {

// Error de configuracion (variable de entorno faltante o invalida).
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Error de MySQL (conexion o consulta), con el mensaje del servidor.
class DatabaseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Datos para conectarse.
struct ConnectionConfig {
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string user = "app_user";
    std::string password;
    std::string database = "academic_db";

    /*
     * Lee la configuracion de las variables TT_DB_* (ver arriba).
     *
     * Errores:
     *   Lanza ConfigError si falta TT_DB_PASS o si TT_DB_PORT no es un
     *   puerto valido.
     */
    static ConnectionConfig from_environment();

    /*
     * Describe el destino sin la contrasena, para mensajes: por ejemplo
     * "MySQL academic_db en 127.0.0.1:3306 (usuario app_user)".
     */
    std::string describe() const;
};

// Un campo de una fila de resultado: el texto que envia MySQL o
// std::nullopt si es NULL.
using Field = std::optional<std::string>;

// Una conexion abierta.
class Connection {
public:
    /*
     * Abre la conexion por TCP, con utf8mb4 y un limite de 5 segundos para
     * conectarse.
     *
     * Errores:
     *   Lanza DatabaseError si no se puede conectar.
     */
    explicit Connection(const ConnectionConfig& config);

    /*
     * Cierra la conexion.
     */
    ~Connection();

    /*
     * Una conexion no se copia: dos objetos cerrarian la misma conexion.
     */
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /*
     * Ejecuta una instruccion que no devuelve filas (SET, START
     * TRANSACTION, COMMIT...).
     *
     * Errores:
     *   Lanza DatabaseError con el mensaje del servidor.
     */
    void execute(const std::string& sql);

    /*
     * Ejecuta una consulta y entrega cada fila a `on_row` conforme llega
     * del servidor (sin cargar todo el resultado en memoria antes).
     *
     * Parametros:
     *   sql     consulta
     *   on_row  funcion que recibe los campos de cada fila
     *
     * Errores:
     *   Lanza DatabaseError si la consulta o la lectura fallan; deja pasar
     *   las excepciones que lance on_row.
     */
    void query(const std::string& sql, const std::function<void(const std::vector<Field>&)>& on_row);

    /*
     * Version del servidor MySQL (por ejemplo "8.4.6").
     */
    std::string server_version() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::mysql

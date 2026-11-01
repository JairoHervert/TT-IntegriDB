// ui_server.hpp
//
// Servidor HTTP de la interfaz del caso de estudio. Entrega la pagina
// (archivos de ui/web) y atiende las llamadas que hace la pagina:
//
//   GET  /api/status    estado de delta local y del probador (JSON)
//   POST /api/preview   cuerpo: la consulta en texto; SQL y AIT (JSON)
//   POST /api/query     cuerpo: la consulta en texto; veredicto y
//                       resultado verificado (JSON)
//   GET  /...           archivos de la pagina (index.html, app.js, ...)
//
// Decisiones:
//   - Escucha por omision en 127.0.0.1: el veredicto viaja sin proteccion
//     entre este proceso y el navegador, asi que ese tramo debe quedarse en
//     la misma maquina (o en una red de confianza, como la red interna de
//     Docker). Entre este proceso y el probador el canal no importa, porque
//     la respuesta se verifica aqui.
//   - Las llamadas POST deben traer la cabecera X-TT-UI. Un sitio ajeno
//     abierto en el mismo navegador no puede agregarla sin una consulta
//     previa (CORS) que este servidor no autoriza, asi que no puede mandar
//     consultas al probador a nombre del usuario.
//   - Todas las respuestas llevan Content-Security-Policy (solo recursos del
//     mismo origen) y nosniff; la pagina escribe los datos de la base como
//     texto, nunca como HTML.
//   - La consulta se limita a net::kMaxQueryBytes, igual que en el probador.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "tt/ui/portal.hpp"

namespace tt::ui {

// Puerto por omision de la interfaz.
inline constexpr int kDefaultUiPort = 8080;

// Cabecera que deben traer las llamadas POST de la pagina.
inline constexpr const char* kUiHeader = "X-TT-UI";

// Opciones del servidor de la interfaz.
struct UiServerOptions {
    std::string host = "127.0.0.1";  // interfaz donde escucha
    int port = kDefaultUiPort;       // 0: el sistema elige un puerto libre (pruebas)
    std::size_t threads = 8;         // hilos para atender conexiones (el navegador abre varias)
    std::filesystem::path web_dir;   // carpeta con index.html, app.js y styles.css
};

// Registro de una peticion que termino en error (ruta inexistente, metodo no
// permitido, cabecera faltante...), para la bitacora.
struct HttpErrorRecord {
    std::string client;
    std::string method;
    std::string path;
    int status = 0;
};

using HttpErrorLogger = std::function<void(const HttpErrorRecord&)>;

// Servidor de la interfaz. No se puede copiar.
class UiServer {
public:
    /*
     * Prepara el servidor; todavia no abre el puerto.
     *
     * Parametros:
     *   portal   logica de la interfaz; debe vivir mas que el servidor
     *   options  interfaz, puerto, hilos y carpeta de la pagina
     *   errors   recibe las peticiones que terminan en error (puede ser vacio)
     *
     * Errores:
     *   Lanza std::invalid_argument si web_dir no tiene index.html.
     */
    UiServer(Portal& portal, UiServerOptions options, HttpErrorLogger errors = {});

    /*
     * Detiene el servidor si sigue escuchando.
     */
    ~UiServer();

    UiServer(const UiServer&) = delete;
    UiServer& operator=(const UiServer&) = delete;

    /*
     * Abre el puerto. Con port = 0 el sistema elige uno libre.
     *
     * Devuelve:
     *   El puerto abierto.
     *
     * Errores:
     *   Lanza net::NetworkError si el puerto esta ocupado o la interfaz no
     *   existe.
     */
    int bind();

    /*
     * Atiende peticiones hasta que se llame stop(). Bloquea el hilo.
     */
    void run();

    /*
     * Espera a que run() este listo para atender (para las pruebas).
     */
    void wait_until_ready() const;

    /*
     * Detiene el servidor; se puede llamar desde otro hilo.
     */
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::ui

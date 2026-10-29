# cpp-httplib (copia fija)

Biblioteca de HTTP/1.1 de un solo archivo de cabecera, usada por el modulo
`net/` para comunicar el probador (servidor) con el verificador (cliente).

- Proyecto: https://github.com/yhirose/cpp-httplib
- Version: v0.58.0 (commit `4f3f9ef19be83ae97a5d9a059432dc00e445b7ab`)
- Archivo: `httplib.h`, sin modificaciones
  (SHA-256 `aa14e7e7bd2703694e0a6b6855af3b8c406102ab1fc56ac905fe33619b31faa5`)
- Licencia: MIT (ver `LICENSE`)

## Por que se copia al repositorio

- La compilacion no depende de descargar nada ni de la version que tenga el
  sistema: en la VM y en los contenedores de Docker se usa exactamente el
  mismo archivo.
- Solo se compila sin TLS (no se define `CPPHTTPLIB_OPENSSL_SUPPORT`). El
  esquema no depende del canal para la integridad: el verificador revisa
  todo lo que recibe contra delta y la clave publica.

## Como actualizarla

1. Descargar `httplib.h` y `LICENSE` de la etiqueta nueva.
2. Reemplazar los dos archivos de esta carpeta y actualizar version, commit y
   SHA-256 en este README.
3. Compilar y ejecutar `ctest`; en particular `net_test`.

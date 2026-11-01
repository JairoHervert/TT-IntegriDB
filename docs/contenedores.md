# Empaquetado en contenedores

Cada bloque de la arquitectura corre en su propio contenedor, y se
levantan juntos con `docker compose`:

| Contenedor | Imagen | Qué corre | Qué recibe |
|---|---|---|---|
| `tt_db` | `mysql:8.4` | Servidor SQL (no confiable) | la base D |
| `tt_prover` | `tt-integridb/prover` | `tt_prover serve` (bloque Servidor) | `deploy/prover`: δ, D~ y pk |
| `tt_verifier` | `tt-integridb/verifier` | `tt_verifier ui` (bloque Cliente) | `deploy/verifier`: δ y pk |

El Propietario de datos (`tt_owner init`, `setup` y `publish`) se queda
fuera de Docker, en la VM. Es el único que tiene la clave secreta, y su
trabajo termina al publicar lo que recibe cada módulo.

```
                 red "data"                 red "clients"
  tt_db  <────────────────────>  tt_prover  <───────────────────>  tt_verifier
  :3306                            :7070                              :8080
    │                                │                                  │
127.0.0.1:3306                 127.0.0.1:7070                    127.0.0.1:8080
(tt_owner setup)              (tt_verifier en la VM)              (navegador)
```

## Cómo usarlo en la VM

Requisito: que `deploy/` ya exista. Es el mismo `publish` de antes:

```bash
./build/owner/tt_owner publish --keys keys --state setup --out deploy
```

Levantar los tres contenedores, desde la raíz del repositorio:

```bash
export TT_DB_PASS=app_pass_2026
docker compose up -d --build
docker compose ps                    # esperar "healthy" en los tres
```

La primera vez, `--build` compila todo dentro de Docker: instala las
dependencias, clona ate-pairing, compila el proyecto y corre las 40
pruebas. Eso tarda unos minutos. Las siguientes veces solo recompila lo que
cambió.

Después:

- **Interfaz:** `http://127.0.0.1:8080` en el navegador de la VM.
- **Línea de comandos dentro del contenedor del verificador:**

  ```bash
  docker compose exec verifier tt_verifier query C4 6 8 --keys /srv/tt/verifier --delta /srv/tt/verifier/delta.txt
  docker compose exec verifier tt_verifier ping --delta /srv/tt/verifier/delta.txt
  ```

- **Desde la VM (el probador publica 7070 en 127.0.0.1):**

  ```bash
  ./build/verifier/tt_verifier query C14 1 --keys deploy/verifier --delta deploy/verifier/delta.txt
  ```

- **Bitácoras:** `docker compose logs -f prover verifier`
- **Detener sin borrar datos:** `docker compose stop`. Para quitar los
  contenedores: `docker compose down`. La base se conserva en su volumen.
- **Solo la base, como antes:** `docker compose up -d db`

Si cambias el código, `docker compose up -d --build` reconstruye las
imágenes y reemplaza los contenedores. Si vuelves a ejecutar Setup y
publish, basta `docker compose restart prover verifier`, porque δ, D~ y pk
se leen al arrancar.

## Nube y cliente en máquinas distintas

`docker-compose.yml` levanta los tres servicios en una sola máquina, que
es lo cómodo para la VM y para las pruebas. Para el despliegue real, la
base y el probador van en la nube con `compose.server.yml`, y el
verificador con la interfaz va en la computadora del cliente con
`compose.client.yml`. Los dos usan las mismas imágenes; los pasos están en
`docs/nube.md`.

## Decisiones y su justificación

### 1. Un Dockerfile con tres etapas

- **`build`** compila ate-pairing (fijado en el commit `530223d`) y el
  proyecto, y corre `ctest`. Si una prueba falla, no se construye ninguna
  imagen.
- **`prover`** y **`verifier`** parten de Ubuntu 24.04, igual que la VM.
  Cada una copia solo su ejecutable y las bibliotecas que usa en tiempo de
  ejecución. El verificador no lleva cliente de MySQL, porque no lo
  necesita.

Cada imagen pesa unos 95 MB. Tener una sola etapa de compilación garantiza
que el probador y el verificador salen del mismo código y de la misma
versión de ate-pairing.

### 2. Ninguna imagen contiene claves ni estado

- **Volúmenes de solo lectura:** δ, D~ y pk llegan al ejecutar, desde
  `deploy/`.
- **Solo se copia el código:** el Dockerfile copia solo las carpetas del
  código (`core/`, `net/`, `ui/`, etc.), nunca la raíz completa. Por eso
  `keys/`, `setup/`, `deploy/` o una compilación local en `build/` no
  pueden terminar dentro de una imagen, ni siquiera en la etapa `build`.
- **`.dockerignore`:** es una segunda barrera. Deja fuera esas mismas
  carpetas y cualquier `secret_key*.txt`.
- **La misma imagen sirve para cualquier estado:** después de un nuevo
  Setup basta reiniciar.

### 3. Dos redes: el verificador no ve la base

`db` y `prover` comparten la red `data`; `prover` y `verifier` comparten
`clients`. El verificador solo puede hablar con el probador: dentro de su
contenedor el nombre `db` ni siquiera existe. Así se cumple en la
infraestructura lo que dice la arquitectura: el cliente nunca toca D, solo
recibe (R, π) del servidor.

### 4. Puertos publicados solo en 127.0.0.1

- **8080 (interfaz):** por lo explicado en `docs/interfaz.md`, el
  veredicto viaja sin protección hasta el navegador. Dentro del contenedor
  la interfaz escucha en 0.0.0.0, porque es la única forma de que Docker la
  alcance, pero en la VM solo se publica en 127.0.0.1.
- **7070 y 3306:** solo se publican para usar `tt_verifier` y
  `tt_owner setup` desde la VM.

### 5. Usuario sin privilegios

Los dos programas corren como el usuario `tt` (uid 10001), no como root.
Leen sus archivos de volúmenes de solo lectura y no escriben nada.

### 6. La contraseña de MySQL no se escribe en el archivo para el probador

En `docker-compose.yml`, `TT_DB_PASS:` va sin valor, así que Compose la
copia de la terminal donde se hizo `export TT_DB_PASS=...`. Si no se
exportó, el probador no arranca y su bitácora dice exactamente eso.

El servicio `db` sí conserva la contraseña de desarrollo con la que se creó
la base, como estaba antes.

### 7. Salud de cada servicio

| Servicio | Se considera sano cuando |
|---|---|
| `db` | `mysqladmin ping` responde |
| `prover` | `GET /v1/health` responde con su encabezado |
| `verifier` | la interfaz entrega su página |

Las comprobaciones de las imágenes propias usan `bash` en lugar de `curl`
para no agregar paquetes.

El arranque sigue ese orden: el probador espera a que la base esté sana y
el verificador espera al probador.

### 8. Detención limpia

`docker stop` envía SIGTERM, y los dos programas ya la atienden: dejan de
escuchar y terminan con código 0 en menos de un segundo, sin esperar los 10
segundos tras los que Docker los mataría a la fuerza.

## Lo que se probó

- Las dos imágenes se construyeron desde cero, con las 40 pruebas pasando
  dentro de la etapa `build`.
- Ninguna imagen contiene `secret_key*`, δ ni D~.
- Los tres contenedores quedan sanos.
- **Desde la interfaz y la línea de comandos:**
  - `ping` coincide con δ.
  - C4 6 8 da 9822 filas, C9 da 500 y C14 1 da 3465 décimas de crédito,
    igual que fuera de Docker.
  - El expediente completo en el navegador se verifica en ambos casos:
    dentro del contenedor y desde la VM contra el puerto 7070.
- **Alteración:** con `grade = 8` en `record_id = 14`, C2, C4 y C14 se
  rechazan señalando la fila. Al regresarla, todo vuelve a verificar.
- **Aislamiento:** desde `tt_verifier`, `db` no se resuelve; desde
  `tt_prover`, sí.
- **Detención:** `docker compose stop prover verifier` tarda 0.6 s y los
  dos terminan con código 0 y el aviso de SIGTERM en su bitácora.
- **Sin `TT_DB_PASS` exportada:** el probador termina con el mensaje "falta
  la variable de entorno TT_DB_PASS" y Docker lo reintenta. Al exportarla y
  recrearlo, se conecta.

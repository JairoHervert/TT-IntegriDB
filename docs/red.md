# Separación de los módulos y comunicación por red

Este documento explica cómo quedaron separados el módulo probador (bloque
Servidor) y el módulo verificador (bloque Cliente), cómo se comunican por la
red y por qué se tomó cada decisión.

## Qué cambió

Antes, el probador escribía la respuesta (R, π) en un archivo y el
verificador la leía. Además, cada programa elegía por su cuenta, con una
cadena de `if` de unas 300 líneas, qué ProofGen o qué Verify usar para cada
consulta del catálogo.

Ahora:

1. **Un servicio único en cada extremo, dentro del núcleo** (`core/`, sin red
   ni MySQL):
   - `query::answer_query(contexto, consulta)` → bytes TTRSP de la respuesta.
   - `query::verify_query(δ, pk, consulta, bytes)` → aceptada o rechazada,
     con el resultado como tabla (columnas y filas; AVG, COUNT, MIN y SUM
     son una tabla de una fila).
   - `query::query_kind` y `query::to_sql(ParsedQuery)` concentran la
     clasificación de las 14 consultas, y `query::required_trees` dice qué
     AIT necesita cada una.
2. **Un adaptador de red** (`net/`): servidor y cliente HTTP.
3. **Programas**:
   - `tt_prover serve`: el probador como servidor.
   - `tt_verifier query`: envía la consulta, recibe (R, π) y la verifica.
   - `tt_verifier ping`: comprueba la conexión y si el probador tiene el
     mismo δ.
   - `tt_owner publish`: separa en carpetas lo que recibe cada módulo.
4. `tt_prover answer` y `tt_verifier verify` (con archivos) se conservan y
   usan los mismos servicios, así que un archivo contiene exactamente los
   bytes que viajarían por la red.

```
 Propietario (tt_owner)
   init, setup ──► keys/ (sk_D, pk)   setup/ (δ, D~)
   publish ──────► deploy/prover/   (δ, D~, pk)
               └─► deploy/verifier/ (δ, pk)            sk_D no se copia

 Bloque Cliente                              Bloque Servidor
 ┌──────────────────────────┐   HTTP/1.1     ┌──────────────────────────┐
 │ tt_verifier query        │  POST /v1/query│ tt_prover serve          │
 │  δ, pk                   │ ─────────────► │  δ, D~, pk (cargados una │
 │  verify_query(bytes)     │ ◄───────────── │  vez)                    │
 │  → resultado verificado  │  (R, π) TTRSP  │  answer_query ──► MySQL D │
 └──────────────────────────┘                └──────────────────────────┘
```

## Protocolo

HTTP/1.1 sobre TCP, puerto 7070 por omisión.

| Ruta | Petición | Respuesta |
|---|---|---|
| `POST /v1/query` | Cuerpo: la consulta en texto, igual que en la línea de comandos (`C4 6 8`, `C7 6 8 20221 20222`, `C8`, `range student admission_term 20221 20231`). Máximo 1 KB. | `200` y el cuerpo es (R, π) en formato TTRSP (`application/octet-stream`). |
| `GET /v1/health` | — | `200` con el estado: id de δ, versión, plan y si cargó la clave pública. |

Errores (el cuerpo es un mensaje en texto):

| Código | Cuándo |
|---|---|
| 400 | Consulta mal escrita, vacía o con caracteres que no son ASCII imprimible. |
| 413 | La consulta excede 1 KB. |
| 422 | La consulta no tiene sus AIT en el δ del probador (RNF05). |
| 500 | El probador no pudo responder: MySQL no responde, falta la clave pública para C7–C14, etc. |
| 404 | Ruta desconocida. |

Formato de `/v1/health`:

```
tt-integridb prover 1
state_id <64 dígitos hexadecimales>
state_version 1
plan queries
public_key yes
```

Códigos de salida del verificador: 0 verificada, 3 rechazada, 1 no se pudo
verificar (faltan δ o pk locales, consulta no soportada, pk que no
corresponde a δ), 2 consulta u opciones mal escritas, **4 el probador no
entregó respuesta** (sin conexión, tiempo agotado o error del servidor).

## Decisiones y su justificación

### 1. HTTP/1.1 con cpp-httplib

- **Por qué no un protocolo propio sobre TCP:** habría que delimitar
  mensajes, manejar lecturas parciales, tiempos de espera, tamaños máximos
  y códigos de error. HTTP ya resuelve todo eso y es un estándar que
  cualquiera puede revisar.
- **Por qué no gRPC:** exige Protocol Buffers y dependencias pesadas para
  dos rutas. La respuesta ya tiene su propio formato binario (TTRSP) con
  lectores estrictos, así que un esquema de serialización adicional no
  aporta nada.
- **Ventajas prácticas:**
  - Se puede probar con `curl`.
  - El `healthcheck` de Docker es una petición a `/v1/health`.
  - La interfaz del caso de estudio habla HTTP con el verificador usando
    la misma biblioteca (ver `docs/interfaz.md`).
- **cpp-httplib** es un solo archivo de cabecera con licencia MIT. Se copió
  en `third_party/cpp-httplib/` con versión y SHA-256 fijados, para que la
  VM y los contenedores compilen exactamente lo mismo sin descargar nada.
  Solo se incluye en los `.cpp` de `net/` y `ui/` (y en sus pruebas);
  ningún encabezado público la expone.

### 2. El canal no forma parte de la base de confianza

La integridad no depende de la red: el verificador revisa cada respuesta
contra δ y pk, y una respuesta alterada en el camino se rechaza igual que
una alterada por el servidor. Por eso no se usa TLS. La confidencialidad
de los datos no es un requisito de este TT. Si se necesitara, cpp-httplib
admite TLS con OpenSSL sin cambiar el núcleo.

Por la misma razón, el cliente tampoco confía en lo que dice el servidor
fuera de los bytes que verifica. Los mensajes de error se muestran
recortados y sin caracteres de control, y `/v1/health` es solo
informativo.

### 3. La consulta viaja como texto

Se envía la misma sintaxis de la línea de comandos. El servidor la vuelve a
leer con el mismo `parse_query` estricto, así que no hay un segundo formato
que mantener. El verificador compara la respuesta contra **su** consulta, no
contra la que diga la respuesta: si el servidor respondiera otra consulta,
el rechazo ya estaba cubierto por los Verify de cada tipo.

### 4. Un servicio único por extremo

La generación y verificación se concentran en `answer_query` y
`verify_query`; así, la red, los archivos y la interfaz utilizan una sola
implementación y evitan divergencias entre vías de ejecución. El verificador
también decodifica según la consulta que hizo: si el servidor manda una
respuesta de otro tipo (por ejemplo, la de C13 cuando se pidió C7), el
lector la rechaza.

### 5. El servidor carga el estado una sola vez

`tt_prover serve` lee δ, D~ (0.1–0.2 s) y pk (0.3–0.7 s) al iniciar, y no
en cada consulta. Si pk no corresponde a δ, no arranca. Si no hay pk,
arranca, pero responde C7–C14 con 500 y lo anuncia en `/v1/health`.

### 6. Consultas de una en una

Las conexiones se atienden con 4 hilos (`--threads`), pero ProofGen se
ejecuta de una consulta a la vez, con un candado:

- ProofGen consume casi todo el procesador, así que procesar dos consultas
  en paralelo no las terminaría antes.
- El módulo de NTL es por hilo (cada hilo que atiende una consulta lo fija
  antes de usar el grupo).
- Así ninguna conexión a MySQL se comparte entre dos consultas.

`/v1/health` sí se responde mientras hay una consulta en curso.

### 7. Límites y tiempos de espera

| Límite | Valor | Motivo |
|---|---|---|
| Tamaño de la consulta | 1 KB | La más larga del catálogo ocupa menos de 100 bytes. |
| Tamaño de la respuesta (cliente) | 256 MB | La mayor sobre la base pequeña (C8) ocupa unos 8 MB; el límite deja margen sin permitir que un servidor agote la memoria del cliente. |
| Recibir la petición (servidor) | 10 s | Una consulta es texto corto. |
| Conectar (cliente) | 5 s | Detectar pronto un probador apagado. |
| Esperar la respuesta (cliente) | 600 s (`--timeout`) | ProofGen de C12 y C14 tarda unos 10 s en el contenedor de desarrollo; en una base mayor puede tardar más. |

### 8. Fallas del lado del servidor

- **MySQL no responde:** el servidor no se cae. Las consultas que leen
  filas responden 500 con el motivo (el verificador termina con código 4),
  y C10–C12, que no leen filas, se siguen respondiendo. Cada consulta abre
  su propia conexión, así que en cuanto MySQL regresa, la siguiente
  funciona.
- **Base modificada después de Setup:** el probador responde con la fila
  como está y lo advierte en su bitácora; el verificador la rechaza.
- **Detenerlo:** con Ctrl+C (SIGINT) o SIGTERM (`docker stop`), termina las
  peticiones en curso y sale con código 0.

### 9. Cada módulo recibe solo lo suyo (`tt_owner publish`)

- Al bloque Servidor: δ, D~ y pk.
- Al bloque Cliente: δ y pk.
- La clave secreta nunca se copia.

Antes de copiar, `publish` comprueba que los tres archivos sean del mismo
Setup. Estas carpetas son las que montará cada contenedor de Docker.

### 10. El servidor tampoco confía en las peticiones

Limita el tamaño, solo acepta ASCII imprimible y lee la consulta con el
`parse_query` estricto (enteros de 64 bits sin ceros a la izquierda, rangos
no invertidos, formas conocidas). Responde con un código y un mensaje, y
registra cada petición en su bitácora (cliente, consulta, código, tamaño,
tiempo y resumen).

## Cómo probarlo en la VM

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
export TT_DB_PASS=app_pass_2026

# Propietario (si ya tienes keys/ y setup/ de antes, basta publish)
./build/owner/tt_owner init --q 16000 --out keys
./build/owner/tt_owner setup
./build/owner/tt_owner publish --keys keys --state setup --out deploy

# Terminal 1: probador
./build/prover/tt_prover serve --state deploy/prover --keys deploy/prover

# Terminal 2: verificador
V=./build/verifier/tt_verifier
A="--keys deploy/verifier --delta deploy/verifier/delta.txt"
$V ping --delta deploy/verifier/delta.txt
$V query C4 6 8 $A --show 5
$V query C10 1 $A
$V query C14 1 $A

# Ataque: alterar la base en el servidor después de Setup
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db -e "UPDATE academic_record SET grade = 8 WHERE record_id = 14"
$V query C4 6 8 $A        # RESPUESTA RECHAZADA (código 3)
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db -e "UPDATE academic_record SET grade = 6 WHERE record_id = 14"
```

Para usar dos máquinas, el probador ya escucha en todas las interfaces
(`--host 0.0.0.0`). En el cliente basta `--server IP:7070` o
`export TT_PROVER_ADDR=IP:7070`. Con VirtualBox en modo NAT hay que agregar
un reenvío del puerto 7070 hacia la VM.

## Lo que se probó

- **Pruebas automáticas** (37 en total, todas pasan):
  - `service_test`: las 14 formas del catálogo y `range` se responden y
    verifican, y el resultado coincide con un oráculo que recorre las
    tablas. Bytes alterados en cinco posiciones, truncados, sobrantes o
    vacíos se rechazan sin excepciones, igual que las respuestas de otra
    consulta, de otro tipo o de otro estado. Cubre también las consultas no
    soportadas, el probador sin pk o sin acceso a D y la base modificada.
  - `net_test`: lo mismo por la red con un servidor en el mismo proceso.
    Además:
    - Los bytes recibidos son idénticos a los que genera el núcleo.
    - Errores 400, 413, 422 y 500, con el mensaje saneado.
    - Límite de tamaño en el cliente.
    - Seis clientes a la vez.
    - Un probador con otro δ (RNF08).
    - El servidor detenido (NetworkError).
  - Pruebas de línea de comandos: `serve` sin estado, dirección con
    `http://`, `ping` sin servidor y `publish` sin estado.
- **De punta a punta con MySQL y la base completa**, en el contenedor de
  desarrollo:
  - C1–C14 por la red coinciden con el banco de consultas del reporte.
  - Alterar una calificación hace que C2 y C4 se rechacen.
  - Con MySQL detenido, C4 recibe 500 y C11 se sigue respondiendo.
  - SIGINT y SIGTERM detienen el servidor limpiamente.

Tiempos del probador por la red en el contenedor de desarrollo (base
pequeña; los de la VM se medirán para el capítulo 8):

| Consulta | Respuesta | Probador | Verificador |
|---|---|---|---|
| C4 6 8 | 4.8 MB | 0.10–0.14 s | 0.16 s |
| C7 6 8 20221 20222 | 695 KB | 6.7 s | 0.34 s |
| C8 | 7.5 MB | 0.4 s | 2.2 s |
| C10 1 | 1.4 KB | 0.01 s | < 0.01 s |
| C12 1 1 | 3.7 KB | 9.8 s | 0.02 s |
| C14 1 | 42 KB | 9.6 s | 0.03 s |

## Lo que sigue

1. **Contenedores:** hechos. El probador y el verificador tienen su imagen y
   se levantan con la base usando `docker compose up -d --build`. Ver
   `docs/contenedores.md`.
2. **Interfaz del caso de estudio:** hecha con `tt_verifier ui`. La página
   habla con el verificador (bloque Cliente), nunca directamente con el
   probador, para que todo lo que muestre esté verificado. Ver
   `docs/interfaz.md`.

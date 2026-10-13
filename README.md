# TT 2026-B143 - Verificación de consultas SQL (basado en IntegriDB)

## Estructura

```
CMakeLists.txt            construcción de todo el código C++
docker-compose.yml        orquesta los servicios (por ahora solo "db")
academic_records_db/      base de datos del caso de estudio (Servidor SQL)
  init/01_schema.sql        esquema: program, student, course, academic_record
  init/02_seed_data.sql     datos sintéticos: 500 alumnos (~15,200 registros)
  tools/                    generador de datos y notas de uso
core/                     núcleo compartido: el esquema (sin MySQL ni red)
  include/tt/crypto/        grupo bilineal BN254, aleatoriedad segura,
                            SHA-256 y Enc (AES-128-CBC)
  include/tt/aso/           claves y acumulador del bloque ASO
  include/tt/ait/           árbol de intervalos autenticado: Setup,
                            RangeCover/Search y su verificación
  include/tt/db/            modelo de tablas y filas, hash de fila y el
                            puerto de lectura de la base (db::Source)
  include/tt/merkle/        árbol de Merkle de las filas de cada tabla
  include/tt/io/            archivos binarios y escritura atómica
  include/tt/scheme/        Init, Setup, digest δ, base autenticada D~,
                            planes de árboles y archivos de claves
  include/tt/query/         consultas C1–C6: ProofGen, Verify y el formato
                            de la respuesta (R, π)
cli/                      utilidades de línea de comandos de los tres programas
mysql/                    adaptador de MySQL (libmysqlclient): lectura de la
                          base para Setup y de filas para el probador
owner/                    Propietario de datos: init, setup y catalog
prover/                   módulo probador (bloque Servidor): answer
verifier/                 Cliente SQL + módulo verificador (bloque Cliente): verify
tests/                    pruebas del núcleo y del adaptador
docs/setup.md             decisiones de Setup y su justificación
docs/consultas.md         decisiones de ProofGen/Verify (C1–C6) y su justificación
```

Los datos incluyen 5 carreras (ISC, IIA, LCD, ITI, ICS), cada una con su
propio plan de 8 semestres. ISC usa el mapa curricular real del plan 2020
de la ESCOM; las otras cuatro usan planes inventados con secuencia
lógica. Cada alumno cursa solo materias de su carrera y en orden.

## Arquitectura del código

Toda la lógica criptográfica del esquema vive en `core/`, que es una
biblioteca sin dependencias de MySQL ni de red. Los tres programas
(`owner`, `prover`, `verifier`) son delgados: leen o escriben datos
(MySQL, archivos, red) y llaman al núcleo. La lectura de la base entra al
núcleo por un puerto (`db::Source`); el adaptador de MySQL está en
`mysql/` y las pruebas usan uno en memoria. Así cada función del esquema
(Init, Setup, ProofGen, Verify) se puede probar por separado (RNF02) y los
tres actores quedan separados (RNF01).

Solo el Propietario de datos conoce el secreto `s`; el probador y el
verificador trabajan con la clave pública `pk`.

## Compilar el código C++ (en la VM)

Dependencias, una sola vez:

```bash
sudo apt install build-essential cmake git libgmp-dev libntl-dev libssl-dev libmysqlclient-dev
git clone https://github.com/herumi/ate-pairing.git ~/ate-pairing
make -C ~/ate-pairing/src
```

Al compilar ate-pairing aparecen advertencias `-Warray-bounds` en
`xbyak.h`; son falsos positivos de GCC reciente sobre ese código y no
afectan (las pruebas de bilinealidad pasan).

Compilar y correr las pruebas, desde la raíz del repositorio:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Los ejecutables quedan en `build/owner/tt_owner`, `build/prover/tt_prover`
y `build/verifier/tt_verifier`. Si ate-pairing está en otra ruta:
`cmake -S . -B build -DATE_PAIRING_DIR=/otra/ruta`.

`ctest` no necesita MySQL. La prueba del adaptador contra la base real es
opcional; con la base levantada:

```bash
TT_TEST_MYSQL=1 TT_DB_PASS=app_pass_2026 ctest --test-dir build -R mysql --output-on-failure
```

## Generar las claves (algoritmo Init)

```bash
./build/owner/tt_owner init --q 16000 --out keys --check
```

- `--q` es la cota superior del tamaño de cualquier conjunto autenticado:
  debe ser al menos el número de filas de la tabla más grande
  (`academic_record`: ~15,200 con el tamaño por defecto, ~29,200 con
  `medium`, ~60,100 con `large`).
- Escribe `keys/public_key.txt` (clave pública `pk`, para el probador y el
  verificador) y `keys/secret_key.txt` (clave secreta `sk_D`, permisos 600,
  nunca sale del propietario). Ninguna de las dos se sube al repositorio.
- `--check` comprueba después la estructura de `pk` (tarda lo mismo que
  Init). Sin `--force` no reemplaza claves existentes: generar otras
  invalidaría todo lo autenticado con las anteriores.

Referencia medida en la VM de pruebas (Ubuntu 26.04, GCC 15.2, un solo
hilo, q = 16000): Init 6.26 s, comprobación de estructura 6.84 s, guardado
0.27 s, `public_key.txt` de 9.4 MB.

## Construir la base autenticada (algoritmo Setup)

Con la base levantada (ver "Levantar la base de datos") y las claves de
Init:

```bash
export TT_DB_PASS=app_pass_2026
./build/owner/tt_owner setup                # plan queries (por defecto)
./build/owner/tt_owner setup --trees full --out setup_full
./build/owner/tt_owner catalog              # consultas del catálogo y sus árboles
```

- Lee la base de MySQL con una instantánea consistente. Los datos de
  conexión salen de `TT_DB_HOST` (127.0.0.1), `TT_DB_PORT` (3306),
  `TT_DB_USER` (app_user), `TT_DB_NAME` (academic_db) y `TT_DB_PASS`
  (obligatoria; no hay contraseña escrita en el código).
- Escribe `setup/authenticated_db.bin` (D~, para el servidor) y
  `setup/delta.txt` (δ, público, para el verificador). Sin `--force` no
  reemplaza un estado existente. Ninguno de los dos se sube al repositorio.
- Muestra el tiempo de cada estructura (RF16) y el id del estado.
- `--trees queries` construye solo los 11 árboles AIT que usan las
  consultas del catálogo; `--trees full` construye los 57 del algoritmo del
  reporte (todos los pares de columnas enteras). La justificación completa,
  con el catálogo y los costos medidos, está en `docs/setup.md`.

Referencia medida en la VM de pruebas (base por defecto, q = 16000):
`queries` 1.24 s y D~ de 2.5 MB; `full` 56.54 s y D~ de 64.1 MB.

## Responder y verificar consultas (C1–C6)

Con el estado de Setup (`setup/`) y las claves (`keys/`):

```bash
export TT_DB_PASS=app_pass_2026
./build/prover/tt_prover answer C4 6 8 --out respuesta.bin
./build/verifier/tt_verifier verify C4 6 8 --response respuesta.bin
```

- `tt_prover answer` (ProofGen) lee δ y D~ de `--state` (por defecto
  `setup`), toma de MySQL las filas que indica el AIT y escribe la
  respuesta (R, π): la prueba del AIT y cada fila con su prueba de Merkle.
  Si alguna fila de la base cambió después de Setup, lo avisa.
- `tt_verifier verify` (Verify) usa solo δ (`--delta`, por defecto
  `setup/delta.txt`) y la clave pública (`--keys`, por defecto `keys`).
  Muestra el resultado verificado (`--show N` filas) o el motivo del
  rechazo. Sale con 0 si verificó, 3 si rechazó y 1 si no pudo verificar.
  `--aso` agrega la comprobación con acumuladores del artículo (redundante
  con los contadores autenticados, y unas 20 veces más lenta en la VM).
- Consultas: `C1 <boleta>`, `C2 <student_id>`, `C3 <course_id>`,
  `C4 <min> <max>` (calificación), `C5 <p1> <p2>` (periodo),
  `C6 <p1> <p2>` (periodo de ingreso) y `range <tabla> <columna> <min> <max>`.
  Una consulta sin AIT en δ se rechaza como no soportada (RNF05).
- Por ahora la respuesta viaja como archivo; con la red será el mismo
  contenido. La justificación y los costos medidos están en
  `docs/consultas.md`.

Para ver que el esquema detecta una base alterada en el servidor, cambia
una calificación después de Setup y repite la consulta (regrésala al
terminar):

```bash
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db -e "UPDATE academic_record SET grade = 8 WHERE record_id = 14"
./build/prover/tt_prover answer C4 6 8 --out respuesta.bin        # avisa que la fila cambió
./build/verifier/tt_verifier verify C4 6 8 --response respuesta.bin   # RESPUESTA RECHAZADA
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db -e "UPDATE academic_record SET grade = 6 WHERE record_id = 14"
```

## Árbol de intervalos autenticado (AIT)

`core/include/tt/ait/` implementa el AIT tal como lo describe el reporte:
árbol binario balanceado con una hoja por clave distinta, clave de cada
nodo interno igual a la máxima de su subárbol izquierdo, valor
`Enc_sk(e_u) || g^{e_u}` con `e_u = ∏ (v^{-1} + s)` y hashes de Merkle
(SHA-256). Cada hash compromete también la clave mínima y máxima del
subárbol y el número de filas, de modo que el verificador comprueba solo
con el digest que una prueba de rango no omite ni agrega filas.
`tests/ait_test.cpp` incluye los ataques de un probador malicioso y una
réplica de `single_d_verify` del IntegriDB original, que acepta un
resultado incompleto que este verificador rechaza.

Referencia medida en la VM de pruebas (15,171 registros): Setup de un AIT
con 15,171 claves distintas ≈ 8.4 s; con 6 claves distintas, 0.01 s. Fuera
de la VM: prueba de un rango de ~200 filas ≈ 3.3 KB; verificarla < 0.1 ms.

## Levantar la base de datos

Copia esta carpeta a una ruta normal de Linux en la VM (no a la carpeta
compartida de VirtualBox: Docker no puede leer `init/` desde ahí) y corre:

```bash
chmod -R a+rX academic_records_db
docker compose up -d
docker compose ps
```

Espera a que `docker compose ps` diga "healthy". La primera vez (volumen
vacío) Docker ejecuta `init/01_schema.sql` y luego `init/02_seed_data.sql`.

Los pasos completos, incluido cómo borrar bases anteriores y qué hacer si
algo falla, están en `academic_records_db/tools/USAGE.txt`.

## Conexión

```
host:           127.0.0.1   (desde la VM; más adelante "db" desde otros contenedores)
puerto:         3306
base de datos:  academic_db
usuario:        app_user
contraseña:     app_pass_2026
```

Revisión rápida:

```bash
docker exec -it tt_db mysql -uapp_user -papp_pass_2026 academic_db -e "SELECT COUNT(*) FROM student; SELECT COUNT(*) FROM academic_record;"
```

## Cambiar el tamaño de los datos

Por defecto son 500 alumnos. Para usar 1000 (o 2000 con `--size large`):

```bash
python3 academic_records_db/tools/generate_synthetic_data.py --size medium --data-only --output academic_records_db/init/02_seed_data.sql
docker compose down -v && docker compose up -d
```

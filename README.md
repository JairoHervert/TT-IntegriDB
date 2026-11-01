# TT 2026-B143 - Verificación de consultas SQL (basado en IntegriDB)

## Estructura

```
CMakeLists.txt            construcción de todo el código C++
docker-compose.yml        orquesta los servicios: db, prover y verifier
compose.server.yml        lado servidor para la nube: db y prover
compose.client.yml        lado cliente: verifier con la interfaz
Dockerfile                imágenes del probador y del verificador
.dockerignore             lo que no entra a las imágenes (claves, estado)
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
  include/tt/query/         consultas C1–C14: ProofGen, Verify, el formato
                            de la respuesta (R, π) y los servicios únicos
                            answer_query / verify_query
cli/                      utilidades de línea de comandos de los tres programas
mysql/                    adaptador de MySQL (libmysqlclient): lectura de la
                          base para Setup y de filas para el probador
net/                      comunicación por red probador ↔ verificador (HTTP)
ui/                       interfaz del caso de estudio: servidor local
                          (tt_ui) y la página en ui/web
third_party/cpp-httplib/  biblioteca HTTP de un archivo (copia fija, MIT)
owner/                    Propietario de datos: init, setup, publish y catalog
prover/                   módulo probador (bloque Servidor): serve y answer
verifier/                 Cliente SQL + módulo verificador (bloque Cliente):
                          query, verify, ping y ui
tests/                    pruebas del núcleo, de la red y del adaptador
docs/setup.md             decisiones de Setup y su justificación
docs/consultas.md         decisiones de ProofGen/Verify (C1–C6) y su justificación
docs/red.md               separación de los módulos y comunicación por red
docs/interfaz.md          interfaz del caso de estudio y sus decisiones
docs/contenedores.md      empaquetado en contenedores y sus decisiones
docs/nube.md              despliegue: base y probador en la nube, cliente local
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
núcleo por dos puertos (`db::Source` y `db::RowStore`); el adaptador de
MySQL está en `mysql/` y las pruebas usan uno en memoria. La red es otro
adaptador (`net/`): el núcleo recibe y entrega bytes, y los dos programas
usan los mismos servicios `query::answer_query` (probador) y
`query::verify_query` (verificador), sea que la respuesta viaje por la red
o en un archivo. La interfaz del caso de estudio (`ui/`) es otro adaptador
del verificador: llama al mismo `verify_query` y la página solo muestra el
veredicto. Así cada función del esquema
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

## Entregar a cada módulo lo suyo (publish)

```bash
./build/owner/tt_owner publish --keys keys --state setup --out deploy
```

Comprueba que δ, D~ y la clave pública sean del mismo Setup y copia:

- `deploy/prover/`: `delta.txt`, `authenticated_db.bin` y `public_key.txt`
  (lo que necesita el bloque Servidor).
- `deploy/verifier/`: `delta.txt` y `public_key.txt` (el bloque Cliente
  nunca recibe D~).

La clave secreta nunca se copia. `deploy/` no se sube al repositorio.

## Probador y verificador por la red

En una terminal, el probador como servidor (se detiene con Ctrl+C):

```bash
export TT_DB_PASS=app_pass_2026
./build/prover/tt_prover serve --state deploy/prover --keys deploy/prover
```

En otra terminal, el verificador envía la consulta, recibe (R, π) y la
verifica:

```bash
./build/verifier/tt_verifier ping --delta deploy/verifier/delta.txt
./build/verifier/tt_verifier query C4 6 8 --keys deploy/verifier --delta deploy/verifier/delta.txt
./build/verifier/tt_verifier query C14 1 --keys deploy/verifier --delta deploy/verifier/delta.txt
```

- El probador escucha en el puerto 7070 (`--port`) de todas las
  interfaces (`--host`). Carga δ, D~ y la clave pública una sola vez.
- El verificador usa `--server host:puerto` o la variable `TT_PROVER_ADDR`
  (por omisión `127.0.0.1:7070`). `--save archivo` guarda también la
  respuesta recibida. Códigos de salida: 0 verificada, 3 rechazada, 1 no
  se pudo verificar, 4 el probador no entregó respuesta.
- El protocolo, las decisiones y cómo probarlo desde otra máquina están en
  `docs/red.md`.

## Interfaz del caso de estudio

Con el probador corriendo (sección anterior), en otra terminal:

```bash
./build/verifier/tt_verifier ui --keys deploy/verifier --delta deploy/verifier/delta.txt
```

y en el navegador de la VM se abre `http://127.0.0.1:8080`. Tiene dos
vistas:

- **Expediente del alumno:** con una boleta arma la ficha, los indicadores
  y el historial. Cada parte es una consulta distinta que se verifica por
  separado.
- **Catálogo de consultas:** las 14 consultas con sus parámetros.

La página no verifica nada: lo hace `tt_verifier` con δ y la clave
pública, y la página solo muestra el veredicto. Detalles y decisiones en
`docs/interfaz.md`.

## Todo en contenedores

La base, el probador y el verificador con la interfaz también pueden correr
cada uno en su contenedor. Con `deploy/` ya publicado:

```bash
export TT_DB_PASS=app_pass_2026
docker compose up -d --build
docker compose ps                  # esperar "healthy" en los tres
```

y la interfaz queda en `http://127.0.0.1:8080`. El verificador está en otra
red que la base y no puede alcanzarla; solo habla con el probador. Detalles
y decisiones en `docs/contenedores.md`.

Para separar las máquinas, con la base y el probador en la nube y el
verificador con la interfaz en la computadora del cliente, se usan
`compose.server.yml` y `compose.client.yml`. Los pasos están en
`docs/nube.md`.

## Responder y verificar consultas con archivos

Sin red, con el estado de Setup (`setup/`) y las claves (`keys/`):

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
- El archivo contiene exactamente los bytes que el probador enviaría por la
  red. La justificación y los costos medidos están en
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
docker compose up -d db
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
docker compose down -v && docker compose up -d db
```

## C7: rango multidimensional con interseccion ASO (octubre 2026)

El comando siguiente implementa el **capitulo 6.5.1** y la **tabla 7.15**:

```bash
# Desde el repositorio: claves y Setup existentes (no regenerar)
export TT_DB_PASS=...           # contraseña real de app_user
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure

./build/prover/tt_prover answer C7 6 8 20221 20222 --keys keys --out respuesta_c7.bin
./build/verifier/tt_verifier verify C7 6 8 20221 20222 --response respuesta_c7.bin
```

Sobre la base pequeña del reporte con semilla 42, se esperan **1 373 filas**.
**No** es suficiente verificar únicamente que la respuesta cumple el predicado: el
verificador valida las dos pruebas RangeCover y la prueba criptográfica de
intersección; si se omite alguna fila, los complementos comparten un ID y la
comprobación de coprimalidad falla.

### Funcionamiento de la prueba de interseccion

1. El probador obtiene los conjuntos `A` (grade) y `B` (term_code) a
   partir de los dos AIT autenticados. Calcula `C=A∩B`, y devuelve solo sus
   filas con pruebas Merkle. No transmite todas las filas de `A` y `B`.
2. Para cada nodo de cobertura `N` demuestra `N = C_N ∪ D_N` con un
   testigo del acumulador de `D_N`. Comprueba:

   `e(acc_G1(C_N), acc_G2(D_N)) = e(acc_G1(N), g2)`.

   Los elementos acumulados son los **inversos** de los `record_id` en el
   campo, según la convención ya usada en el AIT.
3. Para cada par de nodos de las dos dimensiones, demuestra que sus
   complementos son disjuntos. Si los polinomios característicos son
   `P_D` y `P_E`, NTL calcula `U*P_D + V*P_E = 1`. Se publican
   `g2^{U(s)}` y `g2^{V(s)}`, verificados con:

   `e(acc_G1(D),g2^{U(s)}) * e(acc_G1(E),g2^{V(s)}) = e(g1,g2)`.

4. Al no haber coincidencias entre complementos, todas las filas que
   satisfacen **ambos** predicados están en `C`. El verificador comprueba
   además que cada fila publicada pertenece auténticamente a la tabla por
   Merkle, no está duplicada y satisface los dos predicados.

Las pruebas ASO se basan en los acumuladores bilineales y supuestos de
seguridad/extractabilidad de IntegriDB. No se usa el modo opcional `--aso`
de C1–C6: en C7 la prueba es **obligatoria**. El formato de la respuesta
C7 es TTRSP (versión 1, tipo **2**); el tipo **1** de C1–C6 no cambia.

**Advertencia de desempeño:** el generador C7 reconstruye polinomios de
los complementos y utiliza el algoritmo extendido de Euclides de NTL;
para la base de 15 171 filas el costo puede ser notable. El diseño prioriza
la verificación de completitud criptográfica por encima de la eficiencia
óptima. Conviene medir tiempos en tu VM antes de sacar conclusiones.

### Pruebas incluidas

`tests/multirange_test.cpp` cubre resultado correcto contra fuerza bruta,
serialización, rangos vacíos y ataques: omisión, duplicación, alteración de
filas, cambios de digest, alteración del AIT, falsificación de compromiso y
falsificación de testigos de Bézout. `tests/query_test.cpp` incluye la forma
C7. Para una ejecución enfocada:

```bash
ctest --test-dir build -R 'multirange_test|query_test' --output-on-failure
```

El código C7 está en `core/src/query/multirange.cpp`,
`core/src/aso/intersection.cpp`, `core/src/query/response_io.cpp`,
`prover/answer_command.cpp` y `verifier/verify_command.cpp`.

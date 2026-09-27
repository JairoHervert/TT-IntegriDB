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
  include/tt/crypto/        grupo bilineal BN254 (ate-pairing + NTL)
owner/                    Propietario de datos: Init + Setup
prover/                   módulo probador (bloque Servidor)
verifier/                 Cliente SQL + módulo verificador (bloque Cliente)
tests/                    pruebas del núcleo
```

Los datos incluyen 5 carreras (ISC, IIA, LCD, ITI, ICS), cada una con su
propio plan de 8 semestres. ISC usa el mapa curricular real del plan 2020
de la ESCOM; las otras cuatro usan planes inventados con secuencia
lógica. Cada alumno cursa solo materias de su carrera y en orden.

## Arquitectura del código

Toda la lógica criptográfica del esquema vive en `core/`, que es una
biblioteca sin dependencias de MySQL ni de red. Los tres programas
(`owner`, `prover`, `verifier`) son delgados: leen o escriben datos
(MySQL, archivos, red) y llaman al núcleo. Así cada función del esquema
(Init, Setup, ProofGen, Verify) se puede probar por separado (RNF02) y los
tres actores quedan separados (RNF01).

Solo el Propietario de datos conoce el secreto `s`; el probador y el
verificador trabajan con la clave pública `pk`.

## Compilar el código C++ (en la VM)

Dependencias, una sola vez:

```bash
sudo apt install build-essential cmake git libgmp-dev libntl-dev
git clone https://github.com/herumi/ate-pairing.git ~/ate-pairing
make -C ~/ate-pairing/src
```

Compilar y correr las pruebas, desde la raíz del repositorio:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Los ejecutables quedan en `build/owner/tt_owner`, `build/prover/tt_prover`
y `build/verifier/tt_verifier`. Si ate-pairing está en otra ruta:
`cmake -S . -B build -DATE_PAIRING_DIR=/otra/ruta`.

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

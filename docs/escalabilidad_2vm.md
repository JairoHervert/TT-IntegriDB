# Evaluacion de escalabilidad en dos VM

Este procedimiento permite reproducir los escenarios de 1,000 y 2,000
alumnos sin sobrescribir los archivos del escenario de 500 alumnos. Las claves secretas
y el comando Setup pertenecen al propietario, que en este laboratorio utiliza
la VM cliente (192.168.56.101). El servidor (192.168.56.102) solo recibe los
datos del escenario y `deploy/prover`.

## Preparacion de los datos (VM cliente)

El generador solo se ejecuta una vez por escenario; se detiene si ya
existen sus archivos SQL, para evitar sobrescribir datos autenticados.

```bash
cd ~/TT-IntegriDB
bash tools/experimentos/preparar_escala.sh 1000
export TT_SCALE=1000 TT_DB_PASS=app_pass_2026
(cd bench_data/1000 && sha256sum -c datos.sha256)
docker compose -f compose.scale.owner.yml up -d db
docker compose -f compose.scale.owner.yml ps
```

La base de este Compose emplea el puerto local 3307 para no interferir con
MySQL en el puerto 3306. Cada valor de `TT_SCALE` genera un proyecto Compose
y un volumen distintos de los demás escenarios.
Comprobar las cantidades con:

```bash
docker compose -f compose.scale.owner.yml exec -T db \
  mysql -uapp_user -p"$TT_DB_PASS" academic_db \
  -e 'SELECT COUNT(*) AS alumnos FROM student; SELECT COUNT(*) AS registros FROM academic_record;'
```

## Init, Setup y Publish (solo VM cliente)

La tabla mas grande de 1,000 alumnos contiene 29,232 registros academicos
con la semilla 42. Se utiliza `q = 32000`; para 2,000 alumnos, con 60,138
registros, se utiliza `q = 65000`.
Es indispensable generar claves distintas y un nuevo estado autenticado.
No usar `--force`, ni sobrescribir `keys/`, `setup/` o `deploy/`.

```bash
export TT_DB_HOST=127.0.0.1 TT_DB_PORT=3307
export TT_DB_PASS=app_pass_2026
./build/owner/tt_owner init --q 32000 --out bench_data/1000/keys
./build/owner/tt_owner setup --keys bench_data/1000/keys \
  --out bench_data/1000/setup
./build/owner/tt_owner publish --keys bench_data/1000/keys \
  --state bench_data/1000/setup --out bench_data/1000/deploy
```

El propietario debe conservar `bench_data/1000/keys/secret_key.txt` en la
VM cliente. No se copia al servidor ni se incluye en Docker.

## Transferir al servidor (desde la VM cliente)

Copiar la configuracion Compose del servidor y unicamente los archivos
publicables del escenario. La version del codigo debe corresponder a la
utilizada para generar el estado autenticado.

```bash
rsync -av compose.scale.server.yml \
  tt-servidor@192.168.56.102:~/TT-IntegriDB/
ssh tt-servidor@192.168.56.102 'mkdir -p ~/TT-IntegriDB/bench_data/1000/init ~/TT-IntegriDB/bench_data/1000/deploy/prover'
rsync -av bench_data/1000/init/ \
  tt-servidor@192.168.56.102:~/TT-IntegriDB/bench_data/1000/init/
rsync -av bench_data/1000/deploy/prover/ \
  tt-servidor@192.168.56.102:~/TT-IntegriDB/bench_data/1000/deploy/prover/
```

Comprobar el SHA-256 de `01_schema.sql` y `02_seed_data.sql` en ambas
maquinas. El contenido debe ser identico, no solo las cantidades de filas:

```bash
# En ambas VM, desde ~/TT-IntegriDB:
sha256sum bench_data/1000/init/01_schema.sql \
  bench_data/1000/init/02_seed_data.sql
```

## Iniciar el servidor de 1,000 alumnos (VM servidor)

```bash
cd ~/TT-IntegriDB
export TT_SCALE=1000 TT_SCALE_PORT=7071 TT_DB_PASS=app_pass_2026
export TT_ENABLE_DIRECT_BENCH=1
# Confirmar que existen los tres archivos del probador antes de iniciar.
ls -lh bench_data/1000/deploy/prover/
docker compose -f compose.scale.server.yml up -d --build
docker compose -f compose.scale.server.yml ps
```

El nuevo probador escucha en `192.168.56.102:7071`. MySQL permanece
privado dentro de Docker. La base de 500 alumnos sigue almacenada en su
volumen original; no ejecutar `down -v`.

## Iniciar el verificador de 1,000 alumnos (VM cliente)

```bash
cd ~/TT-IntegriDB
export TT_SCALE=1000 TT_PROVER_ADDR=192.168.56.102:7071
export TT_ENABLE_DIRECT_BENCH=1
docker compose -f compose.scale.client.yml up -d --build
docker compose -f compose.scale.client.yml ps
python3 tools/experimentos/validar_equivalencia.py --url http://127.0.0.1:8081 --all
```

La interfaz de escala se abre en `http://127.0.0.1:8081`. Para validar
consultas de alto costo puede aumentarse el tiempo de espera de la herramienta
con `--timeout 600`, sin modificar los datos ni el estado autenticado.
La de 500 alumnos sigue disponible en `http://127.0.0.1:8080` si su
contenedor esta iniciado. Para las mediciones, dejar activo solo el
escenario bajo estudio y evitar ejecuciones concurrentes. En particular,
antes de obtener los tiempos conviene detener (sin borrar sus volumenes)
el escenario de 500 alumnos:

```bash
# En el servidor (192.168.56.102):
docker compose -f compose.server.yml stop prover db
# En el cliente (192.168.56.101):
docker compose -f compose.client.yml stop verifier
```

Para regresar al escenario de 500 alumnos, detener los servicios de escala
en cada VM con el archivo correspondiente y reiniciar los originales:

```bash
# En el servidor:
docker compose -f compose.scale.server.yml stop
docker compose -f compose.server.yml start
# En el cliente:
docker compose -f compose.scale.client.yml stop
docker compose -f compose.client.yml start
```

No utilizar `down -v`. Establecer `TT_SCALE` al detener cada escenario para
que Compose identifique el proyecto correcto.

## Antes de recopilar las mediciones

1. Verificar que la base del cliente y la del servidor tengan los mismos datos
   y los mismos archivos de semilla.
2. Comprobar coincidencia de digest y equivalencia de C1 a C14.
3. Registrar el entorno y mantener constantes los recursos de VirtualBox.
4. Ejecutar los mismos pares de calentamiento y de medicion, con los mismos
   parametros de consulta y condiciones de navegador.
5. Exportar un CSV por consulta, registrando el tamaño de base y los
   parametros utilizados.

## Escenario de 2,000 alumnos

Se aplica el mismo procedimiento sustituyendo `1000` por `2000`, con
`TT_SCALE=2000`, generador `large`, `q = 65000` y
`TT_SCALE_PORT=7072` en la VM servidor. En la VM cliente configurar
`TT_PROVER_ADDR=192.168.56.102:7072` y mantener el frontend en
`http://127.0.0.1:8081`. Usar siempre el directorio
`bench_data/2000/` para sus datos, claves, Setup y archivos publicables.

Antes de iniciar los servicios de otro tamaño, detener los del escenario
actual, con `TT_SCALE` configurada para identificar el proyecto Compose
correcto. No ejecutar `down -v` ni utilizar claves de otro tamaño:

```bash
# En la VM servidor, detener el escenario activo (ejemplo: 1,000).
export TT_SCALE=1000
docker compose -f compose.scale.server.yml stop prover db

# En la VM cliente, detener el escenario activo (ejemplo: 1,000).
export TT_SCALE=1000
docker compose -f compose.scale.client.yml stop verifier
docker compose -f compose.scale.owner.yml stop db
```

Revisar memoria y espacio disponible antes de Init y Setup. El navegador
almacena las muestras en memoria: exportar el CSV al terminar cada consulta,
pues una interrupcion o cierre de pestaña puede perder resultados no
exportados. No intentar verificar una base de 2,000 alumnos con las claves
de 500 o 1,000 alumnos.

# Desglose experimental de AIT, Merkle y ASO

## Objetivo

Observar el costo de operaciones concretas durante la construccion y la
verificacion de consultas que utilizan AIT, Merkle y ASO, como C7, C8, C9
y C14. Las operaciones de rango multidimensional tambien son reutilizadas
por otras consultas del catalogo.

La **metrica principal del TT sigue siendo el tiempo total observado desde el
frontend**, comparando el sistema verificable con SQL directo. El desglose
por etapas es complementario: ayuda a interpretar parte de la diferencia,
pero no sustituye la comparacion entre ambos recorridos.

## Alcance de los cronometros

- `AIT`: llamadas a `tree->range()` y `ait::verify_range()` instrumentadas.
- `Merkle`: construccion del hash de una fila, prueba de inclusion y
  `merkle::verify_inclusion()` instrumentadas. No incluye leer las filas de D.
- `ASO`: generacion y comprobacion de Split/Bezout, incluyendo conversiones
  al campo asociadas a esos testigos. No todos estos segundos son calculos de
  emparejamientos: tambien hay preparacion de entradas y asignaciones.
- `RowStore`: tiempo de `fetch_rows()` acumulado por el servicio del Prover;
  incluye conexion y recuperacion de datos desde la fuente configurada.
- `Otros costos de ProofGen`: tiempo total de generacion menos las cuatro
  categorias anteriores. Incluye construccion de conjuntos, JOIN local,
  asignacion de memoria, validaciones y otras operaciones auxiliares.
- `Otros costos de Verify`: tiempo total de `verify_query()` menos AIT,
  Merkle y ASO medidos. Incluye decodificacion TTRSP, comparaciones, JOIN
  local, materializacion del resultado y operaciones auxiliares.

Las categorias instrumentadas no se anidan unas dentro de otras. Los
cronometros son monotonicamente crecientes (`std::chrono::steady_clock`) y
estan aislados por hilo y por peticion. Se ejecutan tambien cuando un bloque
medido termina con una excepcion. Los valores son duraciones **de pared**,
no tiempo exclusivo de CPU; incluyen posibles desplanificaciones del hilo.

No se debe interpretar `Otros costos` como "criptografia pura", ni atribuir
la diferencia completa con SQL directo exclusivamente a la criptografia.

## Recorrido de las mediciones

El Prover publica tres cabeceras HTTP opcionales:

```text
X-TT-AIT-Seconds: ...
X-TT-Merkle-Seconds: ...
X-TT-ASO-Seconds: ...
```

El Verifier conserva las cabeceras de la version 1. Si faltan o son invalidas,
omite solo el desglose: **nunca** decide el resultado de `Verify` en funcion
de esos tiempos. El frontend los muestra en el panel de desglose y los exporta
al CSV en `server_ait_seconds`, `server_merkle_seconds`,
`server_aso_seconds` y `server_other_seconds`.

`verification_stages` conserva las etapas informadas por el Verifier.
En consultas como C7, C8, C9 y C14 se desglosan AIT, Merkle, ASO y otros
costos; no todas las consultas realizan las mismas operaciones.

Las cabeceras de tiempo **no estan autenticadas**. La confianza para los
experimentos exige un entorno controlado. El valor autenticado del sistema
sigue siendo `(R, pi)`, verificado con `delta` y la clave publica.

## Comprobacion de la instrumentacion

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
node tests/ui_metrics_test.js
export TT_DB_PASS=app_pass_2026
export TT_ENABLE_DIRECT_BENCH=1
docker compose up -d --build --force-recreate
```

Abrir la interfaz, ejecutar primero `C8` y luego `C14 1` en modo verificable.
Confirmar que la respuesta se acepta, que aparecen las etapas y que el CSV
contiene las columnas nuevas. Comprobar que SQL directo siga indicando
*resultado no verificado* y que la comparacion de resultados sea valida.

Para comprobar las cabeceras del Prover, utilizar la direccion publicada
en el escenario activo. Por ejemplo, en la VM servidor del escenario base:

```bash
curl -sS -D - -o /dev/null -X POST -H 'Content-Type: text/plain' \
  --data 'C14 1' http://192.168.56.102:7070/v1/query
```

En los escenarios aislados se utiliza el puerto 7071 (1,000 alumnos) o 7072
(2,000 alumnos). Esos puertos estan vinculados a la IP privada del servidor,
no necesariamente a `127.0.0.1`.

Si no aparecen los nuevos campos, comprobar que se reconstruyo el contenedor
`prover` y no solo `verifier`.

## Metodologia de medicion

1. Congelar la version del codigo y la configuracion de hardware, VM, MySQL y
   plan de arboles; registrar su identificador en los metadatos de cada CSV.
2. Confirmar equivalencia de resultados con SQL directo antes de medir.
3. Medir el tiempo total de frontend de cada modo con el mismo conjunto de
   datos, sin tareas competidoras. Alternar el orden de ejecucion de cada par.
4. Separar calentamientos y series de medicion. Reportar mediana, dispersion
   y percentiles, preservando los datos crudos y valores atipicos.
5. Si se compara contra la version sin instrumentacion, estimar la sobrecarga
   de los propios cronometros con varias corridas equivalentes. No comparar
   directamente maquinas diferentes sin describir sus especificaciones.
6. La primera consulta tras reiniciar es un experimento distinto (arranque en
   frio): no mezclarlo con mediciones posteriores a calentamiento.

El desglose es diagnostico y no forma parte de la evidencia criptografica
que acepta o rechaza el verificador. La actualizacion dinamica del estado
autenticado, tratada en la seccion 7.17 del reporte, permanece fuera del
alcance de los experimentos aqui descritos.

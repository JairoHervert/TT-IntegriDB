# Comparación experimental desde el frontend (capítulo 8)

Esta modalidad **no cambia** el formato TTRSP, la decisión de Verify, los algoritmos
criptográficos ni la sección 7.17 del reporte.

## Dos recorridos comparables

- **Con verificación:** navegador → `tt_verifier ui` (`/api/query`) →
  `tt_prover` (`/v1/query`) → MySQL y ProofGen → respuesta `(R,π)` → Verify →
  JSON y presentación en el navegador.
- **Referencia SQL directa:** navegador → `tt_verifier ui` (`/api/direct`) →
  `tt_prover` (`/v1/direct`) → MySQL con la misma sentencia de `query::to_sql` →
  JSON sin prueba → presentación en el navegador.

La referencia se ejecuta en el **probador**: el verificador no obtiene una
conexión directa a MySQL. Los recorridos comparten red, servidor SQL y
frontend. El modo directo se identifica siempre como **NO VERIFICADO**.

## Activación temporal (solo entorno de pruebas)

La referencia SQL está **deshabilitada por defecto**, tanto en probador como
en la interfaz. No admite instrucciones SQL arbitrarias; solo acepta formas
C1–C14 y utiliza `query::parse_query` + `query::to_sql`.

En la VM, con `deploy/` publicado y las claves existentes:

```bash
export TT_DB_PASS=app_pass_2026
export TT_ENABLE_DIRECT_BENCH=1
docker compose up -d --build --force-recreate
docker compose ps
```

Abrir `http://127.0.0.1:8080`, pestaña **Catálogo de consultas**.
Se muestran dos botones: **Consultar y verificar** y
**SQL directo (sin verificar)**. Ejecutar la misma consulta y parámetros
con ambos botones. El CSV de la sesión registra `execution_mode`,
`browser_total_seconds`, `browser_request_seconds` y los tiempos internos.
Las consultas del expediente siguen utilizando exclusivamente Verify.

En el despliegue dividido, exportar `TT_ENABLE_DIRECT_BENCH=1` **en las dos
máquinas** antes de levantar `compose.server.yml` y `compose.client.yml`.
La ruta directa expone datos **sin autenticación criptográfica ni TLS**;
no activar este modo en un servidor público o un entorno de producción.
Restringir el puerto 7070 con el firewall de la infraestructura a las IP
de pruebas. Desactivar con `unset TT_ENABLE_DIRECT_BENCH` y reconstruir
los contenedores para salir del experimento.

## Qué mide cada columna

| Campo | Fronteras de medición |
|---|---|
| `browser_total_seconds` | Desde el botón hasta el primer pintado (aproximado con dos `requestAnimationFrame`). **Métrica principal**. |
| `browser_request_seconds` | `fetch` del navegador hasta leer el JSON completo; no incluye pintar el resultado. |
| `client_roundtrip_seconds` | HTTP ida/vuelta entre verificador y probador, incluida transferencia. |
| `server_generation_seconds` | ProofGen completo, sin codificación TTRSP: incluye trabajo criptográfico y otras tareas. |
| `server_row_fetch_seconds` | Llamadas a `RowStore::fetch_rows`, con conexión MySQL y lectura de filas. Puede haber tiempos de MySQL fuera de ellas. |
| `server_proofgen_without_fetch_seconds` | Diferencia entre ProofGen y `RowStore`; **no** equivale a criptografía pura. |
| `server_encoding_seconds` | Codificación TTRSP; no es criptografía. |
| `verification_seconds` | Verify completo en C++ y construcción del resultado; incluye decodificación y otras operaciones auxiliares. |
| `direct_sql_seconds` | Abre conexión, ejecuta SQL, lee y convierte todas las filas; excluye el JSON. |
| `direct_encoding_seconds` | Construcción del JSON sin prueba desde el resultado SQL. |
| `network_overhead_estimated_seconds` | Ida/vuelta menos manejo HTTP del servidor (si el dato es coherente). No es latencia de red pura. |
| `response_bytes` | TTRSP `(R,π)` para verificación; JSON para SQL directo. Son formatos distintos. |

**No sumar** las columnas que se contienen mutuamente (por ejemplo, el tiempo
HTTP ya incluye el procesamiento del servidor). Los relojes monotónicos se
usan para intervalos medidos dentro de un proceso; no se restan marcas de
tiempo entre máquinas. Las cabeceras HTTP anunciadas por el probador son
**informativas**, no parte de la prueba de integridad.

## Validar equivalencia antes de medir

Ambos caminos deben devolver la misma cantidad y contenido de filas, aunque
MySQL y AIT no garanticen el mismo orden. La herramienta auxiliar usa un
multiconjunto de filas y tolerancia absoluta de 0.0001 para AVG, porque
MySQL puede redondear decimales que Verify calcula como SUM/COUNT. No escribe
filas ni datos personales en archivos.

```bash
python3 tools/experimentos/validar_equivalencia.py
python3 tools/experimentos/validar_equivalencia.py --all
```

Es preferible iniciar con C1, C4 y C14; `--all` también cubre ambos JOIN.
No comparar tiempos si la equivalencia falla, si hay filas truncadas o si
la base de datos fue alterada después de Setup. Restaurar la base y su
estado autenticado antes de los experimentos.

## Procedimiento de las series de medición

1. Registrar revisión Git, CPU, RAM, virtualización, versiones y recursos de
   cada contenedor. No poner contraseñas en el CSV.
2. Trabajar con navegador y pestaña visibles; cerrar procesos de fondo y
   limitar contención por CPU. Para máquinas virtuales, fijar vCPU y RAM.
3. Ejecutar primero 5 calentamientos que no entren al análisis. Después
   al menos 30 pares válidos por consulta/escala, alternando el orden SQL
   directo / verificable para reducir sesgos de caché y temperatura.
4. No cambiar índices, estado autenticado, volumen ni datos entre los dos
   modos. En cada escala volver a generar claves y Setup con q suficiente.
5. Publicar datos crudos y resumir mediana, media, desviación, p95 y factor
   `tiempo_verificado / tiempo_directo`; informar tamaños de respuesta y
   posibles discrepancias por formatos JSON/TTRSP.
6. Repetir escenarios representativos en servidor remoto, manteniendo el
   mismo navegador y especificando velocidad de enlace y ubicación.

Para el estudio local se realizaron series con 500, 1,000 y 2,000 alumnos.
Los resultados, las excepciones de comparabilidad y las gráficas se documentan
en el capítulo 8 del reporte. Los CSV de cada sesión constituyen la fuente
para las estadísticas; este documento describe el procedimiento, no reemplaza
los datos experimentales.

## Contraste externo rápido de los cronómetros

Para comparar la duración HTTP declarada por el probador con un instrumento
independiente (no aísla la criptografía), usar la IP privada y el puerto
del escenario activo, por ejemplo el de 500 alumnos:

```bash
curl -sS -o /dev/null -D /tmp/c14_headers.txt \
  -w 'curl time_total=%{time_total}s\n' \
  -H 'Content-Type: text/plain' --data 'C14 1' \
  http://192.168.56.102:7070/v1/query
grep -i 'X-TT-' /tmp/c14_headers.txt
```

El tiempo `time_total` de curl debe ser al menos similar al manejo del
servidor más transporte. Para comprobar Verify de manera independiente se
puede guardar la respuesta y ejecutar el verificador de archivos con
`/usr/bin/time -p` (esto añade carga de claves y arranque del proceso).

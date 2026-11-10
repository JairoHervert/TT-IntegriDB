# Series de rendimiento pareadas desde el frontend

Las consultas C1–C14 se comparan como **sistemas completos**, desde el
navegador hasta el pintado aproximado de una respuesta. En el recorrido
verificable se genera `(R,π)` y se ejecuta Verify; el recorrido SQL directo
**no** ofrece pruebas de integridad.

## Preparación

1. Usar una base de datos consistente con su estado autenticado y `delta`;
   comprobar primero la equivalencia con
   `python3 tools/experimentos/validar_equivalencia.py --all` (usar `--url`
   para indicar el frontend de los escenarios de escala).
2. Activar la referencia SQL **solo en un entorno aislado**, tanto en el
   probador como en el verificador. En el despliegue de una sola máquina:

   ```sh
   export TT_DB_PASS=app_pass_2026  # cambiar por la credencial local
   export TT_ENABLE_DIRECT_BENCH=1
   docker compose up -d --build --force-recreate
   docker compose ps
   ```

   Para los escenarios distribuidos se utilizan `compose.server.yml` y
   `compose.client.yml`, o los archivos `compose.scale.*.yml` descritos en
   [escalabilidad_2vm.md](escalabilidad_2vm.md). La variable debe estar
   habilitada en ambos extremos.
3. Abrir el frontend en la máquina cliente (`http://127.0.0.1:8080` en el
   escenario base y `http://127.0.0.1:8081` en los de escala), seleccionar
   **Catálogo de consultas** y una consulta C1–C14. El modo `range` queda
   fuera porque no tiene referencia SQL directa en esta API.
4. Anotar en «Entorno / escala» la configuración real, incluyendo tamaño del
   dataset y `q` si se conoce. Conservar además la revisión Git, vCPU/RAM,
   versión de MySQL, índices, recursos y topología de red en la bitácora del
   experimento; esos metadatos no se obtienen automáticamente del navegador.

## Ejecución y comparabilidad

- Primero hacer una prueba rápida de **0 calentamientos y 1 par**. Para
  resultados finales, usar **5 calentamientos y 30 pares** por consulta y
  escenario. En consultas costosas, como C14, puede tardar bastante: no cerrar
  la pestaña ni iniciar trabajos que compitan por CPU. El orden es `V-D`,
  `D-V`, `V-D`, etc.; un par es la misma consulta con los mismos parámetros.
- El navegador ejecuta los pares **en secuencia** y deshabilita el catálogo,
  parámetros y consultas manuales mientras dura la serie.
- El primer pintado es una **aproximación** tomada tras dos
  `requestAnimationFrame`. Los resultados del servidor (ProofGen, RowStore,
  ida/vuelta y Verify) provienen de sus cronómetros propios. Estas duraciones
  son medidas en fronteras distintas y **no deben sumarse directamente**.
- Tras cada par se comparan columnas, cantidad y **multiconjunto** de filas,
  preservando los duplicados e ignorando el orden. Para C10 (AVG) se admite
  una diferencia absoluta de hasta 0.0001 por el redondeo de MySQL. No se
  acepta una respuesta truncada, no verificada, una pestaña oculta ni tiempos
  no finitos. Un par inválido detiene la serie y **no entra al resumen**.
- «Detener tras este par» completa primero el par activo. Si el navegador se
  cierra, las muestras que aún no se exportaron se pierden.
- El archivo CSV guarda cada ejecución, incluyendo calentamientos y fallos,
  con `benchmark_id`, `benchmark_phase`, `benchmark_pair`, `benchmark_order`,
  `benchmark_environment`, `benchmark_pair_valid` y
  `benchmark_browser_visible`; **no incluye las filas SQL**. Exportar el CSV
  antes de recargar o limpiar el registro (límite de 1000 ejecuciones).

## Análisis

El resumen en la página utiliza **solo los pares válidos de la fase `sample`**.
Muestra media, mediana, desviación estándar muestral y percentil 95 (rango más
cercano) de `browser_total_seconds` para ambas rutas. La diferencia de
medianas es `mediana_verificada - mediana_directa` y el factor es
`mediana_verificada / mediana_directa`.

Estas métricas comparan la experiencia completa; el costo adicional **no es
exclusivamente criptográfico**. También intervienen lecturas RowStore, gestión
de datos, serialización, HTTP y presentación. Los tiempos de las cabeceras
HTTP del probador son informativos, no evidencias autenticadas. El desglose
instrumentado de AIT, Merkle y ASO permite analizar componentes del costo,
pero no constituye por sí solo una medición de criptografía pura.

## Comprobaciones de funcionamiento

Antes de registrar una serie, ejecutar las pruebas del proyecto y comprobar
la interfaz en el navegador:

```sh
ctest --test-dir build --output-on-failure
node tests/ui_metrics_test.js
python3 tools/experimentos/validar_equivalencia.py \
  --url http://127.0.0.1:8081 --all --timeout 600
```

La URL del último comando corresponde a los escenarios de 1,000 y 2,000
alumnos; para 500 alumnos usar `http://127.0.0.1:8080`. Las mediciones
solo deben comenzar tras confirmar que las consultas verificables se aceptan
y que su resultado coincide con el SQL directo. Conservar cada CSV original,
identificando su consulta, parámetros y tamaño del conjunto de datos.

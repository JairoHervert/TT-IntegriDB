# Control del entorno y calibración de los cronómetros

## Propósito

Establecer las condiciones de las series de medición del capítulo 8. La **variable de respuesta principal**
es `browser_total_seconds`: desde el inicio de la consulta en el navegador
hasta el pintado aproximado tras dos `requestAnimationFrame`. Se compara el
recorrido verificable (V) con SQL directo (D), usando la misma consulta,
parámetros y datos. Los tiempos de AIT, Merkle, ASO y RowStore explican
parte del costo; **no reemplazan la métrica extremo a extremo**.

El experimento del frontend realiza pares secuenciales con orden alternado.
Los primeros pares son calentamientos y no entran en las estadísticas.
No se debe presentar el tiempo total adicional como «tiempo criptográfico
puro»: las dos rutas tienen algoritmos y formatos de respuesta distintos.

## 1. Validación del estado antes de cada serie

Antes del primer calentamiento, la interfaz solicita `/api/status` y requiere:

- SQL directo experimental habilitado en el verificador.
- Probador accesible y digest coincidente con el del verificador.
- Metadatos de tablas autenticadas disponibles.

Esta solicitud **no forma parte del cronómetro de consulta**. Una vez comprobado,
se conserva una instantánea común para todas las muestras de esa serie:
`benchmark_state_id`, `benchmark_state_version`, `benchmark_plan`,
`benchmark_tables`, `benchmark_prover_address` y
`benchmark_browser_user_agent`. Los CSV anteriores siguen siendo legibles;
las columnas nuevas simplemente no existen en archivos previos.

La comprobación inicial no detecta todos los cambios que podrían producirse
durante una serie. No se deben modificar MySQL, Setup, los contenedores ni
los índices mientras se toman mediciones. Las respuestas verificables siguen
sujetas al veredicto criptográfico independiente de estas métricas.

## 2. Registrar la configuración física y del software

Antes de las series de medición, ejecutar el registro **en ambas VM**:

```bash
# VM servidor: 192.168.56.102 (MySQL + probador)
bash tools/experimentos/registrar_entorno.sh servidor

# VM cliente: 192.168.56.101 (verificador + navegador)
bash tools/experimentos/registrar_entorno.sh cliente
```

En un despliegue de una sola máquina se conserva el uso anterior sin
argumentos (`local`). El script selecciona el archivo Compose según el rol
y funciona también si se transfirió el proyecto con `rsync` sin `.git`.
La identidad de la revisión Git puede faltar en el servidor; por eso se
registra adicionalmente una huella SHA-256 del contenido del código.

El script consulta los archivos `compose.server.yml` y `compose.client.yml`
según el rol. Para los escenarios de 1,000 o 2,000 alumnos se debe registrar
además la salida de `docker compose -f compose.scale.server.yml ps` en el
servidor y `docker compose -f compose.scale.client.yml ps` en el cliente,
con `TT_SCALE` configurada para el tamaño correspondiente. La huella de
código del script incluye los SQL de inicialización originales, pero no los
archivos generados dentro de `bench_data/`: conservar también
`bench_data/<escala>/datos.sha256` como evidencia del escenario.

El comando crea una carpeta en `~/tt_resultados/entorno_ROL_FECHA_UTC/` con:

- `entorno.txt`: rol y Compose, revisión y estado Git cuando existan, CPU,
  RAM, red IPv4, almacenamiento, SO, herramientas y contenedores del rol.
- `huella_codigo_sha256.txt`: huella del contenido actual de las fuentes,
  SQL de inicialización y Compose, incluidos cambios sin commit.

No registra variables de contraseña ni copia claves. Revisar el informe
antes de compartirlo porque ciertas salidas pueden contener nombres de host
u otras rutas locales. Anotar por separado configuración de VirtualBox,
recursos asignados a cada contenedor, versión real del servidor MySQL,
índices, navegador, conectividad y política energética si influyen.

En el entorno distribuido de VirtualBox, la red Host-Only conecta
`192.168.56.101` (cliente) con `192.168.56.102` (servidor). El puerto
publicado del probador depende del escenario: 7070 para 500 alumnos,
7071 para 1,000 y 7072 para 2,000. El navegador corre en el cliente;
la interfaz de los escenarios de escala utiliza el puerto local 8081.
No mezclar sus resultados con los obtenidos en una sola VM.

Comprobar que las huellas de código sean iguales en las dos VM después de
sincronizar los cambios. Una diferencia requiere investigar los archivos;
la coincidencia no demuestra por sí sola igualdad de datos, claves o estado
criptográfico. La prueba de digest y la equivalencia de resultados siguen
siendo controles independientes. Anotar por separado la asignación de CPU y
RAM a VirtualBox, el equipo físico anfitrión y otras cargas concurrentes.

## 3. Calibración aislada de la instrumentación

Para estimar el costo de invocar el envoltorio `metrics::measure`, compilar
un microbenchmark C++17 independiente, sin MySQL ni bibliotecas criptográficas:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -Icore/src/query tools/experimentos/calibrar_cronometros.cpp \
  -o /tmp/tt_calibrar_cronometros
/tmp/tt_calibrar_cronometros
```

Se alternan siete rondas con `Capture` activado e inactivo, después de dos
calentamientos. El resultado es la mediana de nanosegundos por llamada y
su diferencia. **La microprueba no mide el impacto completo de los
cronómetros sobre C8/C14**: intervienen efectos de caché, planificación,
frecuencia del CPU, número real de invocaciones y compilación. Por eso se
registra como control de instrumentación, no como corrección matemática de
los tiempos de las consultas. Para medir un impacto global se necesitaría
un experimento A/B del código con y sin instrumentación en el mismo entorno.

Los relojes C++ son `std::chrono::steady_clock`. En el navegador se usa
`performance.now()`. Solo se restan marcas temporales dentro de un mismo
proceso; el tiempo del probador anunciado por cabeceras HTTP no se autentica.

## 4. Protocolo de las series de medición

1. Comprobar equivalencia de resultados entre ambos modos mediante
   `python3 tools/experimentos/validar_equivalencia.py --all`. Conservar
   una base consistente con `delta` y Setup.
2. Registrar entorno, huella de código y calibración. Evitar procesos
   concurrentes, actualizaciones y cargas ajenas a la prueba.
3. Desde la misma interfaz y navegador, ejecutar una prueba de humo de
   0 calentamientos y 1 par. Revisar CSV y validez.
4. Por cada consulta/parámetros/escala, realizar 5 calentamientos y
   **al menos 30 pares medidos** con orden alternado V-D/D-V. Para consultas
   largas, preparar previamente una ventana de ejecución suficiente.
5. Exportar el CSV inmediatamente. No eliminar valores extremos sin un
   criterio de exclusión previo; registrar interrupciones y fallas aparte.
6. Repetir sesiones independientes cuando sea viable. Reportar por consulta
   mediana, media, desviación muestral, p95, diferencia de medianas y factor
   de ralentización, además de intervalos o dispersión entre sesiones.
7. Analizar el desglose de etapas como información diagnóstica. Las métricas
   anidadas **no** se suman entre sí; `other_seconds` incluye operaciones
   no criptográficas.

**Nota metodológica:** la duración medida hasta dos cuadros de animación es
una aproximación al pintado, no una medición de fotones visibles en pantalla.
El tiempo de presentación cambia con el navegador, la tasa de refresco y
la carga gráfica. Se registra el navegador y se recomienda no cambiarlo
entre rutas ni entre sesiones comparadas.

El protocolo de medicion no sustituye las comprobaciones criptograficas
ni incluye evaluacion de actualizaciones dinamicas del estado autenticado
(seccion 7.17 del reporte).

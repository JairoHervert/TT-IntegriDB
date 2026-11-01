# Interfaz del caso de estudio

La interfaz es una página web de control escolar que muestra expedientes y
consultas del catálogo. Todo lo que muestra pasó por el esquema de
verificación. La sirve el propio verificador con `tt_verifier ui`, así que la
página no verifica nada por su cuenta: cada consulta se envía al probador
por la red, se verifica en C++ con δ y la clave pública, y la página
solo pinta el veredicto (verificada, rechazada o sin respuesta) y el
resultado verificado.

```
navegador ──HTTP──> tt_verifier ui ──HTTP──> tt_prover serve ──> MySQL (D)
 (página)          127.0.0.1:8080            :7070
                   δ + pk                    δ + D~ + pk
                   query::verify_query       query::answer_query
```

## Cómo usarla en la VM

```bash
cmake --build build -j2

# Terminal 1: probador (igual que antes)
export TT_DB_PASS=app_pass_2026
./build/prover/tt_prover serve --state deploy/prover --keys deploy/prover

# Terminal 2: verificador con la interfaz
./build/verifier/tt_verifier ui --keys deploy/verifier --delta deploy/verifier/delta.txt
```

Después se abre `http://127.0.0.1:8080` en el navegador de la VM. Se
detiene con Ctrl+C.

Opciones de `tt_verifier ui`:

| Opción | Por omisión | Para qué |
|---|---|---|
| `--server host:puerto` | `TT_PROVER_ADDR` o `127.0.0.1:7070` | dirección del probador |
| `--keys`, `--delta` | `keys`, `setup/delta.txt` | material público del cliente |
| `--host` | `127.0.0.1` | dónde escucha la interfaz |
| `--port` | `8080` | puerto de la interfaz |
| `--web` | `ui/web` del código | carpeta de la página |
| `--timeout` | `600` | espera máxima por cada respuesta del probador |

La terminal de `tt_verifier ui` lleva una bitácora de cada consulta, con su
veredicto y sus tiempos, por ejemplo:

```
[ui] "C14 1" -> verificada: SUM = 3465 decimas de credito (346.5 creditos); ...; probador 7.89 s (41.9 KB), verificacion 0.03 s
[ui] "C2 1" -> RECHAZADA: la fila con record_id = 14 no es autentica: ...
```

## Las dos vistas

### Expediente del alumno

Se escribe una boleta y la página arma el expediente. Cada parte del
expediente es una consulta distinta al probador y cada una se verifica por
separado:

| Parte | Consulta | Por qué así |
|---|---|---|
| Ficha (nombre, boleta, ingreso) | `C1 <boleta>` | selección por boleta |
| Programa | `range program program_id p p` | rango de un solo valor sobre el AIT de program |
| Historial | `C2 <student_id>` | todas las evaluaciones del alumno |
| Nombres y créditos de las unidades | `range course course_id min max` | rango sobre el AIT de course con los course_id del historial |
| Promedio general | `C10 <student_id>` | AVG |
| Evaluaciones no acreditadas | `C11 <student_id>` | COUNT de grade < 6 |
| Calificación mínima | `C13 <student_id>` | MIN |
| Evaluaciones extraordinarias | `C12 <student_id> 2` | COUNT por tipo |
| Evaluaciones a título de suficiencia | `C12 <student_id> 3` | COUNT por tipo |
| Créditos aprobados | `C14 <student_id>` | SUM tras el JOIN |

El sello del expediente es verde solo si todas las partes se verificaron.
Si alguna se rechazó, el sello es rojo, esa parte no muestra datos y la
columna "Verificación" dice el motivo. Si una boleta no existe, la ficha
dice que no hay alumno con esa boleta. Esa ausencia también está
verificada: el AIT prueba que ninguna fila de `student` tiene esa boleta.

### Catálogo de consultas

Se elige cualquiera de las 14 consultas (o un rango libre sobre una columna
con AIT) y se llenan sus parámetros. Antes de enviarla, la página muestra
el SQL equivalente y los AIT con que se verificará. Esa vista previa la
calcula el verificador con el mismo `parse_query`, y si δ no tiene los
árboles la consulta ni siquiera se envía (RNF05). Al consultar se muestra:

- el sello y el veredicto (con el motivo si se rechazó);
- el tiempo del probador (ProofGen y red, con el tamaño recibido) junto al
  del verificador, para ver la diferencia de costo entre los dos;
- el resultado verificado, en tablas de 50 filas por página o como un
  solo valor para AVG, COUNT, MIN y SUM.

## Rutas que usa la página

| Ruta | Cuerpo | Respuesta |
|---|---|---|
| `GET /api/status` | | δ local (id, plan, tablas, columnas con AIT para rangos) y estado del probador (si responde y si su δ coincide) |
| `POST /api/preview` | la consulta en texto, por ejemplo `C4 6 8` | SQL y AIT, sin contactar al probador |
| `POST /api/query` | la consulta en texto | veredicto, tiempos y resultado verificado |
| `GET /...` | | archivos de `ui/web` |

Ejemplo de respuesta de `/api/query` (recortada):

```json
{"query":"C10 1","catalog_id":"C10",
 "sql":"SELECT AVG(grade) FROM academic_record WHERE student_id = 1",
 "trees":["academic_record(student_id -> grade)"],
 "state":{"id":"da3a3972...","version":1},
 "prover":{"address":"127.0.0.1:7070","status":200,"seconds":0.011,"bytes":1454},
 "verification":{"seconds":0.0018,"stages":[]},
 "outcome":"verified","message":"","detail":"SUM 393 / COUNT 53 = AVG 7.415094",
 "result":{"columns":["AVG(grade)"],"row_count":1,"truncated":false,"rows":[[7.41509433962264]]}}
```

`outcome` puede ser `verified`, `rejected` (con `message` y sin `result`),
`no_answer` (el probador no respondió o respondió con error; `prover.status`
trae el código HTTP o 0 si no hubo conexión), `invalid` (400) o
`unsupported` (422).

## Decisiones y su justificación

### 1. La página no verifica; solo muestra

La verificación se queda en C++, en `query::verify_query`, que es el mismo
código que usan `tt_verifier query` y `tt_verifier verify`. Reescribir
Verify en JavaScript habría duplicado la parte más delicada del esquema. El
navegador recibe el veredicto ya decidido.

### 2. La interfaz es un comando del verificador

`tt_verifier ui` es el bloque Cliente de la arquitectura (Cliente SQL +
módulo verificador): tiene δ y pk, habla con el probador y entrega R al
usuario junto con el veredicto. Un programa aparte habría necesitado otro
canal hacia el verificador y otra copia de δ y pk. El código está en su
propio módulo (`ui/`, biblioteca `tt_ui`), igual que la red está en `net/`:
es un adaptador más y el núcleo no sabe que existe.

### 3. Escucha solo en 127.0.0.1 por omisión

Entre el probador y el verificador el canal no importa, porque todo se
verifica al llegar. Entre el verificador y el navegador sí importa: ahí
viaja el veredicto ya decidido, y quien pueda modificarlo puede pintar
"verificada" sobre datos falsos. Por eso la interfaz escucha por omisión
solo en la misma máquina. Con `--host 0.0.0.0` (por ejemplo, dentro de
Docker) el programa avisa que ese tramo debe estar en una red de confianza.

### 4. Las consultas exigen la cabecera `X-TT-UI`

Un sitio ajeno abierto en el mismo navegador podría intentar mandar
peticiones a `127.0.0.1:8080`. No puede agregar una cabecera propia sin
una consulta previa de CORS, que este servidor no autoriza. Así, solo la
página de la interfaz puede pedir consultas al probador.

### 5. Los datos se tratan como texto

La página escribe todo lo que viene de la base con `textContent`, nunca
como HTML. Además, el servidor envía `Content-Security-Policy` con
`default-src 'self'` y `nosniff`. Un nombre de alumno con etiquetas HTML se
vería como texto y no se ejecutaría.

### 6. Nada se envía al probador si no se puede verificar

`/api/query` lee la consulta y revisa los AIT de δ antes de contactar al
probador, igual que `tt_verifier query`. Las consultas mal escritas (400) o
no soportadas (422) se informan sin generar tráfico. La vista previa usa
el mismo camino.

### 7. Nombres del programa y de las unidades con rangos verificados

El historial (C2) trae `course_id`, no el nombre de la unidad. Para
mostrar nombres sin salir del esquema, la página hace una consulta de rango
verificada sobre `course` con los `course_id` del historial (unas 45 filas
y 18 KB). Usar C8 traería las 15,171 filas de la tabla (unos 7.5 MB) para
un solo alumno. Lo mismo pasa con el programa: un rango de un valor sobre
`program` en lugar de C9 completo. La página une las dos respuestas
verificadas por `course_id`.

### 8. Las partes del expediente se piden de una en una

El probador procesa una consulta a la vez, así que pedirlas todas juntas
no ahorraría tiempo. En orden, la página puede ir llenando cada parte
conforme se verifica, y C14 (la más lenta, unos 8 s en el contenedor) va
al final. Un expediente completo tarda unos 11 s, de los cuales el
verificador ocupa menos de 0.1 s.

### 9. La página es HTML, CSS y JavaScript sin bibliotecas externas

Son tres archivos en `ui/web/`, sin compilación ni CDN: la VM puede no
tener internet durante una demostración y la política de contenido solo
permite recursos del mismo origen. Se sirven desde la carpeta (no
incrustados en el ejecutable), para poder ajustarlos sin recompilar.

### 10. El resultado completo viaja al navegador, con un tope

Las tablas se paginan en el navegador (50 filas por página). El servidor
manda hasta 200,000 filas por consulta. C8, la más grande, son 15,171 filas
y cerca de 1 MB de JSON. Si una base más grande excediera el tope, la
respuesta lo indica con `truncated`.

## Demostración de una base alterada

Con la interfaz abierta en el expediente de la boleta `2020650001`:

```bash
docker exec -e MYSQL_PWD="$TT_DB_PASS" tt_db mysql -uapp_user academic_db -e "UPDATE academic_record SET grade = 8 WHERE record_id = 14"
```

Al consultar de nuevo el expediente:

- El historial (C2) se rechaza y la página señala la fila `record_id = 14`.
- Los créditos aprobados (C14) también se rechazan.
- El promedio (C10) y los conteos siguen verificados, porque se calculan
  con D~ y no leen esa fila de MySQL.
- El sello del expediente queda en rojo.

Para regresar la base a como estaba:

```bash
docker exec -e MYSQL_PWD="$TT_DB_PASS" tt_db mysql -uapp_user academic_db -e "UPDATE academic_record SET grade = 6 WHERE record_id = 14"
```

## Pruebas

`tests/ui_test.cpp` levanta en el mismo proceso un probador y la interfaz
sobre la base pequeña en memoria (sin MySQL) y comprueba:

- El escritor de JSON: escapes, comas, NaN y enteros extremos.
- Las 14 formas del catálogo (y `range`) a través de `/api/query`, con el
  resultado igual al de un oráculo que recorre las tablas.
- Las consultas mal escritas o no soportadas no llegan al probador.
- Un probador con otro δ produce `rejected` y `/api/status` lo avisa.
- Un probador sin clave pública (C7) o detenido produce `no_answer`.
- En el servidor HTTP:
  - la página con sus cabeceras de seguridad;
  - 403 sin `X-TT-UI`, 413 con una consulta demasiado larga y 404 en JSON;
  - ningún archivo fuera de `ui/web` se puede leer.

`ctest` también comprueba que `tt_verifier ui` sin δ termina con un mensaje
claro y código 1.

## En contenedores

Con `docker compose up -d --build`, el verificador con la interfaz corre en
el contenedor `tt_verifier`. Dentro escucha en `0.0.0.0:8080`, pero Docker
solo publica ese puerto en `127.0.0.1` de la VM (`127.0.0.1:8080:8080`),
así que la decisión 3 se mantiene. Ver `docs/contenedores.md`.

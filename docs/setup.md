# Setup del propietario: decisiones y su justificación

Este documento explica cómo quedó implementado el algoritmo Setup
(`tt_owner setup`) y por qué, con los argumentos para defender cada
decisión. Los números son mediciones reales sobre la base del caso de
estudio (500 alumnos, 15,171 registros académicos) en la VM de pruebas.

## Qué hace Setup

Con la base `D` y la clave secreta `sk_D`, el propietario produce:

- **La base autenticada D~** (`setup/authenticated_db.bin`), que se entrega
  al servidor junto con la base. Contiene, por cada tabla, el árbol de
  Merkle de sus filas y, por cada árbol AIT del plan, el árbol completo.
- **El digest δ** (`setup/delta.txt`), que es público y lo usa el
  verificador. Contiene la versión del estado, la huella de la clave
  pública, la estructura de cada tabla con la raíz de sus filas y la raíz
  de cada AIT. Todo se resume en un identificador de 32 bytes (`id`).

Antes de construir nada revisa las condiciones que el esquema impone a los
datos: que la clave pública corresponda a la secreta, que ninguna tabla con
árboles tenga más filas que `q` y que ninguna columna usada como valor de
un AIT contenga 0 (se acumula `v^{-1}` y 0 no tiene inverso).

## Decisión 1: qué árboles AIT se construyen (planes `queries` y `full`)

### Lo que dicen el reporte y el artículo

El algoritmo Setup del reporte, igual que el de IntegriDB, construye un AIT
por cada par de columnas `(i, j)` de cada tabla, incluido `i = j`: `m²`
árboles por tabla. El artículo explica por qué: en consultas anidadas
cualquier columna podría hacer de columna de referencia ("This is the
reason we need to build an AIT for each pair of columns during setup"), y
para sumar una columna sobre el resultado de un rango esa columna debe ser
el valor del árbol. Es decir, `m²` es el precio de poder responder
**cualquier** consulta. El propio artículo trata ese costo como algo que se
puede mejorar ("this can be improved to linear in the number of columns",
sección 5.1).

### Por qué el plan por defecto es `queries`

1. **Nuestro esquema no admite SQL arbitrario.** El reporte delimita un
   subconjunto de consultas (sección "Consultas consideradas") y RNF03 pide
   operar sobre "consultas previamente definidas dentro del alcance del
   esquema". La razón de los `m²` árboles (admitir cualquier consulta) no
   aplica.
2. **El reporte ya lo anticipa.** En "Inicialización del esquema" dice: *"En
   una etapa posterior de implementación, esta construcción podría
   ajustarse para generar únicamente las estructuras necesarias según el
   conjunto de consultas soportadas."* El plan `queries` es exactamente eso.
3. **Es el mismo criterio que ya se aplicó al modelo de datos.** El reporte
   excluyó del modelo la información que ninguna consulta usa porque "el
   costo de la etapa de inicialización del esquema crece de forma
   cuadrática con el número de columnas". El plan `queries` aplica el mismo
   criterio a los pares de columnas.
4. **Cada árbol está justificado por una consulta concreta.** El plan no se
   eligió a mano: es la unión de los árboles que necesita cada consulta del
   catálogo (tabla siguiente). `tt_owner catalog` imprime esta relación.

### El catálogo de consultas y sus árboles

Las consultas salen de la tabla "Relación entre el modelo de datos y las
consultas consideradas" y de los ejemplos de "Consultas consideradas". Los
árboles de cada una siguen los procedimientos de IntegriDB: selección y
rango usan `(columna filtrada → columna de referencia)`; rango en varias
columnas interseca con el ASO los identificadores de un árbol por columna;
JOIN usa los árboles `(i → i)` de ambas columnas; SUM/AVG necesitan la
columna sumada como valor; MIN/MAX se reducen a un rango.

| Consulta | Qué obtiene | Árboles |
|---|---|---|
| C1 | Datos de un alumno por su boleta | student(enrollment_number → student_id) |
| C2 | Registros académicos de un alumno | academic_record(student_id → record_id) |
| C3 | Datos de una unidad de aprendizaje | course(course_id → course_id) |
| C4 | Registros con calificación en un intervalo | academic_record(grade → record_id) |
| C5 | Registros entre dos periodos | academic_record(term_code → record_id) |
| C6 | Alumnos con periodo de ingreso en un intervalo | student(admission_term → student_id) |
| C7 | Rango en calificación y periodo a la vez | grade → record_id, term_code → record_id |
| C8 | JOIN registros con su unidad de aprendizaje | academic_record(course_id → course_id), course(course_id → course_id) |
| C9 | JOIN alumnos con su programa | student(program_id → program_id), program(program_id → program_id) |
| C10 | Promedio de calificaciones de un alumno | academic_record(student_id → grade) |
| C11 | Evaluaciones no acreditadas de un alumno | student_id → record_id, grade → record_id |
| C12 | Evaluaciones de un alumno bajo una forma de evaluación | student_id → record_id, evaluation_type → record_id |
| C13 | Calificación mínima o máxima de un alumno | student_id → record_id, grade → record_id |
| C14 | Créditos aprobados de un alumno | student_id → record_id, grade → record_id, course(course_id → course_id) |

La unión son **11 árboles**: 6 de `academic_record`, 3 de `student`, 1 de
`course` y 1 de `program`. El plan `full` construye 57 (6² + 4² + 2² + 1²,
solo columnas enteras). De esos 57, **46 no los usa ninguna consulta del
catálogo**.

### La seguridad no cambia

- Cada consulta se verifica con los mismos árboles en ambos planes, y esos
  árboles son idénticos byte por byte (misma raíz en δ). La prueba
  automática `setup_test` lo comprueba.
- La lista de árboles construidos queda dentro de δ, que publica el
  propietario. El servidor no puede fingir que un árbol no existe ni
  inventar uno.
- Si alguien pide una consulta que necesita un árbol que no está en δ, esa
  consulta no se puede verificar y se rechaza como no soportada (RNF05).
  Nunca se acepta una respuesta sin prueba. El plan limita **qué** se puede
  preguntar, no **qué tan seguro** es lo que se responde.

### Costo medido

Base del caso de estudio (4 tablas, 15,906 filas), medido en la VM de
pruebas (Ubuntu 26.04, GCC 15.2, un solo hilo) con claves de q = 16000:

| | `queries` | `full` |
|---|---|---|
| Árboles AIT | 11 | 57 |
| Tiempo de Setup | 1.24 s | 56.54 s |
| Tiempo total del comando | 1.64 s | 57.88 s |
| Tamaño de D~ | 2.5 MB | 64.1 MB |
| Tamaño de δ | 2.8 KB | 8.8 KB |

`full` es unas 45 veces más lento y su D~ pesa unas 25 veces más. La
memoria máxima del proceso, medida fuera de la VM, fue de unos 31 MB con
`queries` y 200 MB con `full`.

En `full`, 55.3 de los 56.4 s de árboles se van en los 46 árboles que nadie
usa, y 51.2 s (más del 90 %) solo en los 6 árboles con `record_id` como
clave, de 15,171 hojas cada uno. Ninguna consulta del caso de estudio
filtra por rangos de `record_id`. Los 11 árboles que ambos planes
comparten suman lo mismo en las dos corridas (unos 1.1 s).

El costo de un árbol depende de cuántas claves distintas tiene, no de
cuántas filas acumula: cada nodo lleva sus acumuladores en G1 y G2, y un
árbol con k claves distintas tiene 2k − 1 nodos. Por ejemplo,
`grade → record_id` acumula las 15,171 filas pero tiene 6 hojas y tarda
0.01 s, mientras que `record_id → grade` tiene 15,171 hojas y tarda 8.4 s.
Por eso casi todo el costo de `full` lo pagan los árboles cuya clave es la
columna de referencia, y el costo total no es simplemente m² veces el de
un árbol.

### Por qué se conserva `full`

- Reproduce el algoritmo Setup del reporte tal cual, como referencia.
- Permite medir el costo cuadrático y compararlo con `queries`; es un
  resultado que se puede reportar (RF16, RNF04).
- Si en el futuro se agrega una consulta, basta con agregarla al catálogo
  (`core/src/scheme/tree_plan.cpp`); `full` sirve mientras tanto.

### Preguntas probables

- **"¿No pierde generalidad?"** Sí, a propósito y dentro del alcance: el
  esquema no promete SQL arbitrario. Con `full` se recupera la generalidad
  del artículo.
- **"¿Qué pasa si mañana necesitan otra consulta?"** Se agregan sus árboles
  al catálogo y se vuelve a ejecutar Setup; el nuevo δ los incluye.
- **"¿Por qué el costo no crece exactamente como m²?"** Porque cada árbol
  cuesta según sus claves distintas. Los árboles con la columna de
  referencia como clave tienen una hoja por fila y son los caros; en el
  caso de estudio son 6 de los 57 y se llevan más del 90 % del tiempo.
- **"¿Por qué no un solo árbol por columna (costo lineal)?"** El artículo lo
  logra con un árbol de Merkle homomórfico, solo para SUM; cambiaría el
  esquema y queda fuera del alcance.
- **"¿El servidor podría negarse a responder diciendo que falta un
  árbol?"** La lista de árboles está en δ; si el árbol existe y el servidor
  no entrega la prueba, la respuesta se rechaza.

## Decisión 2: un árbol de Merkle por tabla para las filas

El Setup del reporte pone en δ un hash `h_r` por cada fila. Con 15,171
registros eso son cientos de KB que el cliente tendría que guardar y
actualizar después de cada cambio. El propio reporte indica que "el tamaño
de δ puede reducirse a O(1) mediante la construcción de un árbol de Merkle
adicional sobre estos valores", y el artículo de IntegriDB dice que así lo
hace su implementación ("as we do in our implementation").

- δ guarda solo la raíz de cada tabla (32 bytes) y su número de filas.
- El servidor demuestra que una fila es auténtica con una prueba de
  inclusión de `⌈log₂ n⌉` hashes (14 para `academic_record`).
- Estructura de RFC 6962/9162 (Certificate Transparency): definida para
  cualquier número de hojas sin duplicar la última (duplicar hojas permite
  dos listas distintas con la misma raíz). Las hojas van ordenadas por la
  columna de referencia.
- El hash de fila compromete todas las columnas, incluidas las de texto,
  con marcas de tipo y longitudes, así que el entero 1 y el texto "1", o
  ("ab", "c") y ("a", "bc"), nunca dan el mismo hash.

## Otras decisiones

- **δ en texto, con id y versión.** Es pequeño y legible (se puede mostrar
  en la presentación). Al leerlo se recalcula el `id` y se compara; si
  alguien edita el archivo, se rechaza. `state_version` empieza en 1 y
  no cambia automáticamente cuando se modifica la base. La actualización
  incremental del estado autenticado tras INSERT/DELETE no está implementada
  y permanece pendiente en la sección 7.17 del reporte.
- **Huella de la clave pública en δ.** Hash de toda `pk`; el verificador
  comprueba que tiene la misma `pk` con la que se construyó el estado.
- **Lectura consistente de MySQL.** Toda la base se lee dentro de
  `START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY`: δ describe un
  estado que realmente existió aunque alguien escriba al mismo tiempo.
- **Setup determinista.** Con la misma base y las mismas claves, δ y D~
  salen idénticos byte por byte (RNF04: repetir experimentos).
- **D~ primero, δ al final.** δ es el que publica el estado; si algo falla
  a la mitad, nunca queda un δ nuevo junto a un D~ viejo. Ambos se escriben
  de forma atómica y se mandan al disco (`fsync`).
- **Contraseña de MySQL solo por variable de entorno** (`TT_DB_PASS`), para
  no dejarla escrita en el código.

## Relación de Setup con ProofGen y Verify

El estado generado por Setup se utiliza en la generación y verificación de
las consultas C1–C14. Los valores de las hojas de cada AIT y la lista de
referencias de cada tabla no están cubiertos directamente por las raíces
de δ. Los valores se comprometen mediante los acumuladores correspondientes;
las filas obtenidas de la base D se comprueban con pruebas de Merkle. Esta
separación permite detectar cambios en MySQL posteriores al estado autenticado.

En la implementación actual, δ contiene la raíz de Merkle de cada tabla
en lugar de un hash por fila. El plan `queries` genera únicamente los AIT
necesarios para las consultas C1–C14; el plan `full` permite construir el
conjunto completo de árboles del esquema.

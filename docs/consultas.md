# Consultas de selección y rango (C1–C6): decisiones y su justificación

Este documento explica cómo quedaron implementados ProofGen (`tt_prover
answer`) y Verify (`tt_verifier verify`) para las consultas de selección y
de rango sobre una columna, y por qué, con los argumentos para defender
cada decisión.

## Qué se puede hacer ahora

Por primera vez una consulta va de punta a punta:

```bash
export TT_DB_PASS=app_pass_2026
./build/prover/tt_prover answer C4 6 8 --out respuesta.bin       # el servidor responde
./build/verifier/tt_verifier verify C4 6 8 --response respuesta.bin   # el cliente verifica
```

El probador lee D~ y δ (lo que le entregó el propietario), toma de MySQL
las filas necesarias y escribe la respuesta (R, π) en un archivo. El
verificador, que solo tiene δ y la clave pública, acepta o rechaza esa
respuesta. La respuesta puede viajar como archivo o por la red
(`tt_prover serve` y `tt_verifier query`, ver `docs/red.md`); en los dos
casos son los mismos bytes y el núcleo no cambia.

Consultas que se aceptan (las mismas en los dos programas):

| Forma | Consulta |
|---|---|
| `C1 <boleta>` | `SELECT * FROM student WHERE enrollment_number = <boleta>` |
| `C2 <student_id>` | `SELECT * FROM academic_record WHERE student_id = <student_id>` |
| `C3 <course_id>` | `SELECT * FROM course WHERE course_id = <course_id>` |
| `C4 <min> <max>` | `SELECT * FROM academic_record WHERE grade BETWEEN <min> AND <max>` |
| `C5 <p1> <p2>` | `SELECT * FROM academic_record WHERE term_code BETWEEN <p1> AND <p2>` |
| `C6 <p1> <p2>` | `SELECT * FROM student WHERE admission_term BETWEEN <p1> AND <p2>` |
| `range <tabla> <columna> <min> <max>` | rango en cualquier columna entera que tenga AIT en δ |

Códigos de salida del verificador: 0 verificada, 3 rechazada, 1 no se pudo
verificar (archivos faltantes, consulta no soportada, clave pública que no
corresponde a δ), 2 consulta mal escrita.

## Qué contiene la respuesta (R, π)

- El id del estado (δ) sobre el que se respondió y la consulta que responde.
- La prueba de RangeCover del AIT `(columna filtrada → columna de
  referencia)`: el árbol podado de la parte 5 (nodos de cobertura,
  expandidos y podados).
- Cada fila del resultado, completa, con su prueba de inclusión en el árbol
  de Merkle de su tabla (⌈log₂ n⌉ hashes). Las filas van en el orden del AIT.

## Cómo verifica

El verificador acepta solo si, en este orden:

1. La respuesta es del estado y de la consulta que él tiene (RNF08).
2. La prueba del AIT reconstruye la raíz publicada en δ. Así obtiene,
   autenticados, los nodos de cobertura del rango, el intervalo
   `[min, max]` de claves de cada uno y cuántas filas hay debajo.
3. Hay exactamente tantas filas como suman esos contadores.
4. Cada fila es auténtica (su hash, con su prueba de Merkle, reconstruye la
   raíz de su tabla en δ) y ninguna se repite.
5. A cada nodo de cobertura le tocan, en orden, tantas filas como dice su
   contador, y todas tienen la columna filtrada dentro de su intervalo.

Si algo falla, rechaza con el motivo y no entrega ninguna fila.

## Decisión 1: para C1–C6 basta con los contadores; el ASO es opcional

### El argumento

El procedimiento del artículo para un rango compara el acumulador de los
identificadores devueltos con los acumuladores de los nodos de cobertura.
En nuestro esquema eso es **redundante** para C1–C6, por esta razón:

- Las hojas del AIT están ordenadas por clave. Por lo tanto, el subárbol de
  un nodo contiene **todas** las claves de la tabla que caen en su intervalo
  `[min, max]`, y su contador es el número de filas con esas claves.
- El contador y el intervalo están dentro del hash del nodo (decisión de la
  parte 5), así que están autenticados contra δ.
- Si el servidor entrega exactamente ese número de filas **distintas**,
  **auténticas** (Merkle contra δ) y con la clave dentro del intervalo, son
  exactamente esas filas: no existen otras.
- La forma canónica de la prueba del AIT garantiza que los nodos de
  cobertura abarcan todo `[lo, hi]`.

Entonces no puede faltar ni sobrar ninguna fila. En el IntegriDB original
el hash del nodo no compromete el contador ni el intervalo, y por eso ahí
el acumulador sí es necesario.

### Por qué importa

La comprobación con el acumulador es, con mucho, lo más costoso de la
verificación: hay que combinar |R| + 1 potencias de la clave pública. En la
VM, con las 9,822 filas de C4 6 8, la verificación completa tarda 1.03 s
con ella (0.97 s solo en los acumuladores) y 0.05 s sin ella: unas 20
veces menos.

### Lo que se conserva

`tt_verifier verify ... --aso` hace además la comprobación del artículo,
nodo por nodo. Sirve para comparar tiempos y como verificación "literal".
Las pruebas automáticas verifican cada respuesta en los dos modos y todos
los ataques se rechazan en ambos.

### Dónde sí hace falta el ASO

En las consultas que combinan conjuntos (rango en dos columnas C7, C11–C13;
JOIN C8–C9; SUM/AVG C10, C14) el resultado ya no coincide con nodos
completos, y ahí el ASO es indispensable (intersección y suma). Es la
siguiente etapa.

## Decisión 2: el probador toma las filas de la base D

D~ guarda, por cada fila, solo su referencia y su hash: lo necesario para
las pruebas de Merkle, no la fila completa. Por eso el probador lee de
MySQL las filas que indica el AIT (`SELECT ... WHERE <referencia> IN
(...)`, en bloques de 1000, dentro de una instantánea consistente).

Esto también hace visible el caso que el esquema debe detectar: si alguien
modifica la base después de Setup, el probador envía la fila tal como está,
avisa al operador del servidor y el verificador la rechaza porque su hash ya
no lleva a la raíz de δ:

```
[prover] AVISO: 1 fila(s) de la base ya no son las que se autenticaron en Setup ...
[verifier] RESPUESTA RECHAZADA: la fila con record_id = 14 no es autentica: su prueba de
           Merkle no lleva a la raiz de academic_record en delta (la fila fue alterada o no existe)
```

## Decisión 3: consultas no soportadas (RNF05)

Una consulta solo se puede verificar si δ tiene el AIT `(columna →
referencia)`. Con el plan `queries` esos son los de C1–C6. Una consulta sin
árbol (por ejemplo `range student program_id 1 2`) se rechaza como no
soportada, tanto en el probador como en el verificador: nunca se acepta una
respuesta sin prueba. Con el plan `full` ese mismo rango sí se puede
verificar.

## Ataques que las pruebas automáticas rechazan

`tests/selection_test.cpp`, en los dos modos de verificación:

- omitir una fila; decir que no hay resultados (con la prueba honesta o con
  la de un árbol vacío);
- cambiar una fila por otra auténtica de fuera del rango; agregar una;
  repetir una fila en lugar de otra; mover filas entre nodos de cobertura;
- cambiar una celda entera, la columna filtrada (aun dentro del rango) o
  una celda de texto; quitar una celda; poner texto en una columna entera;
- intercambiar las pruebas de Merkle de dos filas o usar una posición fuera
  del árbol;
- prueba del AIT incompleta; usar la respuesta de [6, 8] para [6, 9] (con y
  sin reescribir la consulta); una respuesta que dice responder otra
  consulta;
- respuesta de otro estado; respuesta del estado anterior con el δ nuevo, y
  al revés (RNF08);
- base modificada en el servidor después de Setup (calificación cambiada
  dentro o fuera del rango; fila borrada).

Además, cada C1–C6 y 300 rangos aleatorios en 7 columnas se comparan con
el resultado calculado por fuerza bruta. Las pruebas de mutación (romper a
propósito cada revisión del verificador y del formato) detectan 15 de 15
mutaciones. La comprobación del acumulador puesta como "siempre acepta"
sobrevive, como era de esperarse por la decisión 1.

## Costos medidos

Base del caso de estudio (15,171 registros), plan `queries`, q = 16000,
medido en la VM de pruebas (Ubuntu 26.04, GCC 15.2, un solo hilo). El
tiempo del probador incluye cargar D~, recorrer el AIT, leer las filas de
MySQL, generar las pruebas y guardar la respuesta.

| Consulta | Filas | Respuesta | Probador (total) | Verificar | Verificar con `--aso` |
|---|---|---|---|---|---|
| C1 (una boleta) | 1 | 1.6 KB | 0.12 s | < 0.01 s | < 0.01 s |
| C2 (un alumno) | 53 | 28 KB | 0.10 s | < 0.01 s | 0.01 s |
| C3 (una materia) | 1 | 1.5 KB | 0.10 s | < 0.01 s | < 0.01 s |
| C4 6 8 | 9,822 | 4.8 MB | 0.19–0.25 s | 0.05 s | 1.03 s |
| C5 20221 20222 | 2,150 | 1.1 MB | 0.13 s | 0.01 s | 0.23 s |
| C6 20221 20231 | 106 | 39 KB | 0.10 s | < 0.01 s | 0.01 s |
| C4 0 100 (toda la tabla) | 15,171 | 7.5 MB | 0.24 s | 0.07 s | 1.57 s |

Además, el verificador tarda 0.34 s en cargar la clave pública (9.4 MB) y
comprobar que su huella sea la de δ, una sola vez por ejecución.

La verificación sin el ASO crece con el número de filas (un hash y una
prueba de Merkle por fila) y se mantiene por debajo de 0.1 s incluso para
la tabla completa. Con `--aso`, el costo de los acumuladores domina: unas
20 veces más.

Unos 500 bytes por fila de la respuesta son la prueba de Merkle (14 hashes
de 32 bytes). Si el tamaño importara, se puede usar una prueba de
inclusión conjunta para filas vecinas, que comparte los hashes comunes.

## Para actualizar en el reporte

- En Verify para rangos, la implementación comprueba los contadores
  autenticados de los nodos de cobertura en lugar de los acumuladores, con
  el argumento de la decisión 1; la comprobación del artículo sigue
  disponible (`--aso`).
- La respuesta incluye el id del estado y la consulta, para que una
  respuesta de otro estado o de otra consulta se rechace con un motivo
  claro (RNF08).

## C13: MAX/MIN por reduccion a rango (IntegriDB §4.4)

`C13 <student_id>` propone la minima calificacion desde el AIT existente
`academic_record(student_id -> grade)` de C10 y **verifica**:

`student_id = id AND grade BETWEEN INT64_MIN AND minimo_propuesto`

mediante el mismo procedimiento criptografico C7 (RangeCover de los AIT,
interseccion ASO y filas autenticadas con Merkle). Solo se acepta el valor
propuesto cuando **todas** las filas del rango tienen ese mismo valor y
existe al menos una fila. Si el AIT autentica que el alumno no tiene filas,
se entrega SQL `NULL`. El transporte vuelve a ser `TTRSP` tipo 2.

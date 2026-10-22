# C13 — MIN(grade) por alumno (IntegriDB §4.4)

## Procedimiento reutilizando AIT + ASO

IntegriDB reduce MIN a un rango autenticado. Para `student_id = 1` el
Prover propone `m = 5` (obtenido del AIT existente `student_id -> grade`)
y genera la prueba de la consulta equivalente:

```sql
SELECT * FROM academic_record
WHERE student_id = 1 AND grade <= 5;
```

El verificador usa **la misma prueba C7** (`MultiRangeResponse`, TTRSP tipo 2):
1. autentica los dos RangeCover contra el digest delta;
2. autentica cada fila con Merkle y el predicado;
3. comprueba con ASO que la interseccion no tiene omisiones;
4. exige que exista por lo menos una fila y que **todas tengan grade = 5**.

Por tanto, hay al menos un 5 y **no existe una calificacion del alumno
menor a 5**. El verificador NO acepta `m` solo porque el Prover lo proponga.

Si el alumno no tiene registros, el RangeCover de `student_id` acredita
`count = 0` y el resultado de MIN es `NULL` (semantica SQL).

Se reutiliza el AIT `student_id -> grade` construido por C10 solo para
calcular el candidato. No se agrega ningun AIT y no se modifica Setup.
El Prover hace una lectura de MySQL por lote para recuperar **solo las
filas con la calificacion minima**, no todos los registros del alumno.

## Aplicar y verificar en Ubuntu

El parche C13 se aplica **sobre C11 y C12 sin commit**:

```bash
cd ~/TT-IntegriDB
git apply --check ~/TT-C13-MIN-IntegriDB-sobre-C11-C12.patch
git apply ~/TT-C13-MIN-IntegriDB-sobre-C11-C12.patch
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build -R min_c13_test --output-on-failure -V
ctest --test-dir build --output-on-failure
```

```bash
./build/prover/tt_prover answer C13 1 --out respuesta_c13.bin
./build/verifier/tt_verifier verify C13 1 --response respuesta_c13.bin
```

Comparacion con el MySQL del estado Setup:

```sql
SELECT MIN(grade) AS minimo
FROM academic_record
WHERE student_id = 1;
```

**Referencia sobre seed 02_seed_data.sql:** para alumno 1 se esperan
53 registros, ocho con `grade = 5` y `MIN = 5`. Validar sobre la VM;
pueden cambiar si se modifica la base tras Setup.

Caso sin registros (puede probarse con un ID sin registros):

```bash
./build/prover/tt_prover answer C13 999999 --out respuesta_c13_null.bin
./build/verifier/tt_verifier verify C13 999999 --response respuesta_c13_null.bin
```

### Seguridad y límites

- El archivo no se autentica por ser TTRSP. Lo autentican RangeCover,
  ASO, Merkle y el digest de confianza.
- La comprobacion de la ausencia de calificaciones menores es **criptografica**:
  cualquier fila omitida invalida la prueba de completitud ASO.
- Los tests automáticos usan datos en memoria. **La compilacion y prueba
  extremo a extremo en Ubuntu todavia estan pendientes** al entregar
  el parche: el entorno de preparacion no dispone de `ate-pairing`.
- No hacer `git add`, `commit` o `push` hasta que C11/C12/C13 sean
  revisadas conjuntamente, como acordamos.

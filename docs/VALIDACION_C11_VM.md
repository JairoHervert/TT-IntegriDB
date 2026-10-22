# Validacion C11 — COUNT de evaluaciones no acreditadas

**No publicar en Git hasta validar C11 y C13:** C12 sigue sin commit a peticion del equipo.

## Consulta del catalogo

```sql
SELECT COUNT(*) FROM academic_record WHERE student_id = 1 AND grade < 6;
```

La condicion `grade < 6` se representa internamente en el AIT de grade con el
rango inclusivo `[INT64_MIN, 5]`: `grade` es un entero y la desigualdad es
estricta. El otro rango es `[1, 1]` en `student_id`.

## Arquitectura y reutilizacion

- Reutiliza `CountQuery`, `answer_count()` y `verify_count()` de C12.
- Dos pruebas `RangeCover`, testigos ASO de separacion y de coprimalidad
  (Bézout), y verificacion del COUNT contra la cardinalidad de IDs autenticados.
- Usa el mismo formato binario **TTRSP tipo 3** de C12, pero incorpora `catalog_id`
  y los predicados para impedir aceptar una respuesta de otra consulta.
- No hace SELECT a MySQL durante ProofGen o Verify. Los AIT corresponden
  al estado creado por Setup; tras cambios de MySQL hay que actualizar ese estado.

## Compilacion y pruebas

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build -R 'count_c11_test|count_intersection_test|query_test' --output-on-failure -V
ctest --test-dir build --output-on-failure
```

## Ejecucion en la VM

```bash
./build/prover/tt_prover answer C11 1 --out respuesta_c11.bin
./build/verifier/tt_verifier verify C11 1 --response respuesta_c11.bin
```

La semilla `small` incluida en el ZIP del 21 de octubre genera **8** evaluaciones
no acreditadas para el alumno 1. Confirmar que el estado D~ y los datos de la VM
proceden de esa semilla. Para comprobar el valor independientemente en MySQL:

```bash
docker exec -it tt_db mysql -u app_user -p academic_db
```

```sql
SELECT COUNT(*) AS total
FROM academic_record
WHERE student_id = 1 AND grade < 6;
```

## Comprobaciones de ataques

`tests/count_c11_test.cpp` comprueba resultado correcto, estado distinto,
consulta distinta, COUNT alterado, referencia omitida, duplicados, testigo de
separacion alterado, testigo de Bézout alterado, caso vacio, serializacion y
regresion entre C11 y C12. La bateria de pruebas no constituye por si misma
una prueba formal de seguridad.

## Siguiente hito

Validar en VM, **no realizar commit todavia**, despues preparar C13 y subir
C12+C11+C13 en un commit conjunto (o tres commits consecutivos en una sola
sesion, segun lo acuerde el equipo).

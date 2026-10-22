# C12 — COUNT(*) autenticado con AIT + ASO (implementación propuesta)

## Consulta del catálogo

```sql
SELECT COUNT(*)
FROM academic_record
WHERE student_id = 1 AND evaluation_type = 1;
```

Se utiliza `C12 <student_id> <evaluation_type>` para el probador y verificador.

## Diseño y consultas a MySQL

El propietario construyó previamente `D~`/AIT con `tt_owner setup` a partir de
MySQL. Para responder C12, `answer_count()` lee solamente los AIT autenticados
`student_id -> record_id` y `evaluation_type -> record_id`, calcula los ID
comunes y prepara testigos ASO. **No realiza consultas adicionales a MySQL**
ni recupera filas completas. Por eso C12 representa el estado **del Setup** y
no necesariamente cambios posteriores de MySQL.

Para permitir verificar el `COUNT` sin confiar en un entero, la respuesta
binaria TTRSP **tipo 3** transporta el escalar, los `record_id` únicos de la
intersección como material de prueba, las particiones de ID por nodo AIT, los
dos `RangeCover` y los testigos ASO de separación y completitud (Bézout).
El verificador comprueba que todos los ID están autenticados en cada rango y
no faltan ID comunes, antes de aceptar su cantidad.

No confundir "COUNT como escalar" con "prueba de tamaño constante": la prueba
contiene O(|R|) identificadores y además testigos por nodos.

## Compilación y pruebas (en VM con NTL/ate-pairing)

```bash
cd ~/TT-IntegriDB
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build -R 'count_intersection|query_test|multirange' --output-on-failure -V
ctest --test-dir build --output-on-failure
```

## Ejecución en la base sintética usada en el TT

```bash
./build/prover/tt_prover answer C12 1 1 --out respuesta_c12.bin
./build/verifier/tt_verifier verify C12 1 1 --response respuesta_c12.bin
```

Resultado esperado del catálogo: **COUNT = 47** para estudiante 1 y forma de
evaluación 1 en la base pequeña con semilla 42. Confirmar el resultado contra
la base instalada, pues puede cambiar si MySQL fue actualizado luego de Setup.

Consulta de control independiente, ejecutable en MySQL desde una sesión SQL:

```sql
SELECT COUNT(*)
FROM academic_record
WHERE student_id = 1 AND evaluation_type = 1;
```

La consulta SQL es solamente una **comparación experimental**; el verificador
no consulta MySQL para decidir la autenticidad.

## Límites y advertencias

- El verificador necesita el `delta.txt` auténtico del propietario y la
  `public_key.txt` vinculada a ese digest.
- El módulo hereda las hipótesis de seguridad y límites de capacidad `q` del
  ASO implementado para C7. Los tests no reemplazan una demostración formal.
- El ZIP original C7 es una instantánea anterior a los últimos cambios de
  la VM. Aplicar con `git apply --check` y no forzar si hay conflictos.
- No regenerar claves, Setup ni volumen Docker para integrar C12.

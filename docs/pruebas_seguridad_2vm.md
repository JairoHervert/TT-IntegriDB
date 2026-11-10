# Pruebas de seguridad en dos máquinas virtuales

Estas pruebas se realizan después de validar que C1 a C14 entregan resultados
correctos. No forman parte de la medición de rendimiento: su objetivo es
comprobar que el verificador rechaza respuestas que no corresponden a la
consulta o al estado publicado por el Propietario.

## 1. Respuestas alteradas sin modificar MySQL

En la **VM cliente** (`192.168.56.101`), donde están el verificador, `pk` y
`delta`, ejecutar:

```bash
cd ~/TT-IntegriDB
python3 tools/experimentos/probar_seguridad.py --server 192.168.56.102:7070
```

El script solicita `C4 6 8` al probador remoto y guarda la respuesta original.
Después ejecuta `verify` sobre copias alteradas. No modifica archivos de
Setup, el estado del probador ni los datos de MySQL.

| Caso | Condición | Veredicto esperado |
|---|---|---|
| S01 | Respuesta original recibida por red | Aceptada (código 0) |
| S02 | Respuesta original desde archivo | Aceptada (código 0) |
| S03 | Un bit alterado en la respuesta | Rechazada (código 3) |
| S04 | Respuesta truncada | Rechazada (código 3) |
| S05 | Byte adicional al final | Rechazada (código 3) |
| S06 | Respuesta de C4 6 8 para C4 6 9 | Rechazada (código 3) |
| S07 | Respuesta de C4 para C5 | Rechazada (código 3) |
| S08 | Byte alterado cerca de la cabecera | Rechazada (código 3) |

El código `1` indica un problema de ejecución, no un rechazo criptográfico;
por tanto, no debe contarse como caso satisfactorio. Los archivos binarios,
los registros de consola y `resumen_seguridad.csv` se guardan en una carpeta
`~/tt_resultados/seguridad_FECHA_UTC/`.

## 2. Alteración controlada de un registro en MySQL

Esta prueba es independiente y **sí modifica temporalmente una fila**. Debe
realizarse solo con la base sintética del laboratorio, después de respaldar
los datos y de confirmar que no se están ejecutando series de rendimiento.

En la **VM servidor** (`192.168.56.102`), comprobar primero el valor actual:

```bash
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db \
  -e 'SELECT record_id, grade FROM academic_record WHERE record_id = 14;'
```

Si la calificación original no es `6`, detenerse y revisar el estado.
Con la calificación original confirmada, ejecutar:

```bash
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db \
  -e 'UPDATE academic_record SET grade = 8 WHERE record_id = 14 AND grade = 6;'
```

En la **VM cliente**, ejecutar la misma consulta verificable:

```bash
cd ~/TT-IntegriDB
./build/verifier/tt_verifier query C4 6 8 \
  --server 192.168.56.102:7070 \
  --keys deploy/verifier --delta deploy/verifier/delta.txt --show 0
```

La respuesta debe rechazarse. Un error del probador o una falla de red
**no es equivalente** a un rechazo del verificador; registrar el código
real de salida y los mensajes. Si el comando se ejecuta en una terminal
interactiva, consultar el código inmediatamente con `echo $?`.

**Restaurar la calificación original en el servidor, incluso si la prueba
falla**, y confirmar que volvió a `6`:

```bash
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db \
  -e 'UPDATE academic_record SET grade = 6 WHERE record_id = 14 AND grade = 8;'
docker exec tt_db mysql -uapp_user -papp_pass_2026 academic_db \
  -e 'SELECT record_id, grade FROM academic_record WHERE record_id = 14;'
```

Finalmente repetir la consulta verificable desde el cliente y confirmar que
vuelve a aceptarse. No volver a ejecutar Setup para ocultar la alteración.

## 3. Evidencia y limitaciones

Conservar el CSV, los archivos de respuesta y los registros de las pruebas.
Los casos S03 a S08 comprueban rechazo de respuestas alteradas mediante el
verificador real, pero no representan por sí solos una demostración formal
de seguridad ni sustituyen el análisis criptográfico del esquema.

Para la sección 8.3 del reporte deben registrarse el caso, la alteración,
el resultado esperado, el código observado y una breve interpretación. No
incluir resultados de una prueba que todavía no se haya ejecutado.

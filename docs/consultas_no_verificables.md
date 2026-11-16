# Consultas no verificables (N1–N5)

El catálogo verificable (C1–C14) cubre las consultas para las que el
esquema tiene un mecanismo de completitud. Para mostrar ese límite, el
sistema acepta además cinco consultas que **no** se pueden verificar. El
probador las ejecuta en MySQL y entrega el resultado sin (R, π); el
verificador lo muestra marcado como no verificado, con un sello naranja en
la interfaz y el código de salida 5 en la línea de comandos.

| Id | Consulta | Tipo excluido | Motivo |
|---|---|---|---|
| `N1 <prefijo>` | Alumnos cuyo apellido paterno empieza con `<prefijo>` | `LIKE` sobre texto | El AIT solo ordena claves enteras; no hay prueba de que no falte ningún apellido. |
| `N2 <student_id>` | Promedio y número de evaluaciones por periodo | `GROUP BY` | Habría que probar cada grupo y que no falte ninguno; el esquema autentica agregados sobre un solo conjunto. |
| `N3 <student_id>` | Calificación máxima | `MAX` | No está en el plan de árboles; su prueba sería simétrica a la de C13 (MIN). |
| `N4 <student_id>` | Kárdex con programa y unidad de aprendizaje | `JOIN` de cuatro tablas | El esquema verifica `JOIN` de dos tablas (C8, C9). |
| `N5 <n>` | Los `n` alumnos con mejor promedio (1–100) | `GROUP BY` + `ORDER BY` + `LIMIT` | Exige probar el orden de un valor derivado de varias filas. |

## Recorrido

```
navegador ──/api/unverified──▶ tt_verifier ui ──POST /v1/unverified──▶ tt_prover
                                     ▲                                   │ MySQL (plantilla SQL)
                                     └──────────── TTUNV, sin prueba ◀───┘
```

```bash
tt_verifier query N2 1            # imprime la tabla con el aviso; código de salida 5
curl -s -H 'X-TT-UI: 1' -X POST --data 'N3 1' http://127.0.0.1:8080/api/unverified
```

## Decisiones

1. **Catálogo fijo, sin SQL libre.** La consulta se lee con reglas
   estrictas (`parse_unverified_query`): enteros en forma canónica y, para
   N1, de 1 a 30 letras ASCII. El SQL sale de una plantilla
   (`unverified_sql`), así que un valor no puede cambiar la forma de la
   sentencia ni agregar comodines. Un cuadro de SQL libre abriría la puerta a
   inyecciones y a consultas costosas, aun con un usuario de solo lectura.
2. **Ruta propia, siempre separada de la verificable.** `/v1/query`,
   `verify` y `/api/query` rechazan N1–N5, y `/v1/unverified` rechaza C1–C14.
   Una consulta verificable nunca puede responderse sin prueba, y una no
   verificable nunca se presenta como verificada.
3. **El verificador escribe el JSON, no el servidor.** El resultado viaja en
   un formato binario propio (TTUNV). `decode_unverified_result` exige el id
   de la consulta pedida, las columnas y tipos esperados, reales finitos y
   que no sobren bytes; después el verificador escribe su propio JSON con
   `outcome: "unverified"`. Un probador malicioso no puede agregar campos
   como `"outcome":"verified"`. Pasar esa revisión solo dice que el formato
   es correcto: **no dice nada de la integridad de los datos**.
4. **Siempre habilitada.** A diferencia de la referencia SQL directa
   (`TT_ENABLE_DIRECT_BENCH=1`), estas consultas no son un experimento de
   rendimiento sino parte de la demostración: siempre están disponibles y
   siempre se marcan.
5. **Sin el candado de ProofGen.** No usan NTL ni el estado autenticado y
   abren su propia conexión a MySQL, de modo que no esperan detrás de una
   prueba larga (por ejemplo, C14).

## Formato TTUNV

```
"TTUNV" | versión u8 = 1 | id (texto) | columnas u32 | (nombre texto, tipo u8)*
| row_count u64 | filas u32 | por celda: etiqueta u8 (0 NULL, 1 entero i64,
  2 real como u64 IEEE 754, 3 texto) y su valor
```

Los enteros van en big-endian y los textos llevan su longitud (u32) por
delante, igual que TTRSP. `row_count` es el total que declara el servidor;
puede ser mayor que las filas enviadas si se recortó por tamaño.

## Pruebas

- `unverified_test`: lectura, plantillas, separación con el catálogo y
  rechazo de TTUNV alterado (otra consulta, columnas cambiadas, tipo de
  celda, real no finito, bytes truncados o sobrantes).
- `net_test`: la ruta `/v1/unverified` con un adaptador simulado, 404 sin
  adaptador y rechazo de SQL libre o de consultas C.
- `ui_test`: vista previa, `/api/unverified` y que un servidor que cambia el
  id o las columnas no logre mostrar nada ni inyectar un veredicto.
- `verifier_unverified_rejects_save` y `verifier_unverified_rejects_wildcard`.

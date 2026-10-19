# Comprobación de C7 en Ubuntu (con tu Setup y las claves existentes)

**No ejecutes `tt_owner init --force`, `tt_owner setup --force` ni `docker compose down -v`.**
Tu instalación ya cuenta con `keys/`, `setup/` y MySQL: se reutilizan sin regenerarlos.

## Instalar cambios mediante el parche

Desde el proyecto `~/TT-IntegriDB` clonado con Git, con la rama `main` limpia:

```bash
cd ~/TT-IntegriDB
git status --short
# Copia el archivo TT-C7-ASO-16OCT.patch de tu carpeta compartida a ~/.
git apply --check ~/TT-C7-ASO-16OCT.patch
git apply ~/TT-C7-ASO-16OCT.patch
git status --short
```

**Si `git apply --check` devuelve un error, no fuerces el parche.** Verifica si alguien modificó `main` desde la versión del 16 de octubre.

## Compilar y pruebas unitarias

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'multirange_test|query_test|selection_test' --output-on-failure
```

## Validación real con MySQL

```bash
read -rsp "Contraseña de MySQL: " TT_DB_PASS; echo; export TT_DB_PASS
./build/prover/tt_prover answer C7 6 8 20221 20222 --out respuesta_c7.bin
./build/verifier/tt_verifier verify C7 6 8 20221 20222 --response respuesta_c7.bin --show 5
```

Con los datos sintéticos pequeños del reporte (semilla 42), se esperan **1,373 filas** y veredicto **RESPUESTA VERIFICADA C7**. Si el generador tarda, mide el tiempo total y anota tamaño del archivo: los XGCD de los complementos requieren aritmética polinómica de alto grado.

Para verificar que las seis consultas previas no se rompieron, ejecuta por ejemplo C4 y C5 nuevamente; `ctest` incluye las pruebas antiguas y las nuevas.

No hagas `git add .` a ciegas si tienes otro trabajo local. Revisa `git status`, después `git add core/ prover/ verifier/ tests/ docs/ README.md` y, solo cuando todo pase, crea tu commit en `main`.

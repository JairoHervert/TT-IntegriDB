#!/usr/bin/env bash
# Genera los datos del escenario sin alterar los 500 alumnos existentes.
# Solo crea archivos locales. No inicia ni modifica contenedores.
set -euo pipefail

if [[ $# -ne 1 || ("$1" != "1000" && "$1" != "2000") ]]; then
    echo "Uso: bash tools/experimentos/preparar_escala.sh 1000|2000" >&2
    exit 2
fi

escala="$1"
raiz="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
destino="$raiz/bench_data/$escala"

if [[ -e "$destino/init/01_schema.sql" || -e "$destino/init/02_seed_data.sql" ]]; then
    echo "[escala] ya hay datos en $destino/init. No se sobrescribieron." >&2
    exit 1
fi

mkdir -p "$destino/init"
cp "$raiz/academic_records_db/init/01_schema.sql" "$destino/init/01_schema.sql"

if [[ "$escala" == "1000" ]]; then
    tamano=medium
    q=32000
else
    tamano=large
    q=65000
fi
python3 "$raiz/academic_records_db/tools/generate_synthetic_data.py" \
    --size "$tamano" --seed 42 --data-only \
    --output "$destino/init/02_seed_data.sql"

(
    cd "$destino"
    sha256sum init/01_schema.sql init/02_seed_data.sql > datos.sha256
)
cat > "$destino/escenario.txt" <<EOF
Escenario: $escala alumnos
Semilla: 42
Cota q propuesta para Init: $q
Datos: bench_data/$escala/init/
Estado autenticado: generar en la VM del propietario, no en el servidor.
EOF
printf '[escala] datos generados en %s\n' "$destino"
printf '[escala] q recomendado: %s\n' "$q"
printf '[escala] verificacion: (cd "%s" && sha256sum -c datos.sha256)\n' "$destino"

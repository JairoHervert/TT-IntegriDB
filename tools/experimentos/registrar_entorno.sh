#!/usr/bin/env bash
# Registra el entorno experimental sin guardar credenciales ni claves.
# Ejecutar en cada maquina con el rol: servidor, cliente o local.
set -euo pipefail

rol="${1:-local}"
case "$rol" in
    servidor) compose_file="compose.server.yml" ;;
    cliente) compose_file="compose.client.yml" ;;
    local) compose_file="docker-compose.yml" ;;
    *)
        printf 'Uso: bash %s [servidor|cliente|local]\n' "$0" >&2
        exit 2
        ;;
esac

# La copia distribuida puede no contener .git (sincronizada por rsync).
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
cd "$repo"
mkdir -p "${HOME}/tt_resultados"
out="${HOME}/tt_resultados/entorno_${rol}_$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out"

{
    printf 'Fecha UTC: %s\n' "$(date -u +%FT%TZ)"
    printf 'Rol experimental: %s\n' "$rol"
    printf 'Archivo Compose: %s\n' "$compose_file"
    if [[ -d .git || -f .git ]] && command -v git >/dev/null 2>&1; then
        printf 'Revision Git: %s\n' "$(git rev-parse HEAD)"
        printf 'Rama: %s\n' "$(git branch --show-current)"
        printf 'Estado Git:\n'
        git status --short
    else
        printf 'Revision Git: no disponible (copia de trabajo sin .git)\n'
    fi
    printf '\nSistema operativo:\n'
    if [[ -r /etc/os-release ]]; then
        grep -E '^(NAME|VERSION_ID|PRETTY_NAME)=' /etc/os-release || true
    fi
    uname -srm
    printf '\nRed IPv4:\n'
    if command -v ip >/dev/null 2>&1; then ip -4 -br addr || true; fi
    printf '\nCPU:\n'
    if command -v lscpu >/dev/null 2>&1; then
        lscpu | grep -E '^(Architecture|CPU\(s\)|Model name|Hypervisor vendor|Virtualization type|Thread\(s\) per core|Core\(s\) per socket):' || true
    fi
    printf '\nMemoria:\n'
    if command -v free >/dev/null 2>&1; then free -h; fi
    printf '\nAlmacenamiento del proyecto:\n'
    df -h . || true
    printf '\nVersiones de herramientas:\n'
    if command -v cmake >/dev/null 2>&1; then cmake --version | head -1; fi
    if command -v g++ >/dev/null 2>&1; then g++ --version | head -1; fi
    if command -v node >/dev/null 2>&1; then node --version; fi
    if command -v docker >/dev/null 2>&1; then
        docker --version
        docker compose version 2>/dev/null || true
        printf '\nContenedores del rol %s:\n' "$rol"
        docker compose -f "$compose_file" ps 2>/dev/null || true
        if [[ "$rol" != 'cliente' ]]; then
            printf '\nVersion del cliente MySQL del contenedor:\n'
            docker compose -f "$compose_file" exec -T db mysql --version 2>/dev/null || true
        fi
    fi
    printf '\nReferencia directa habilitada en esta sesion: '
    if [[ "${TT_ENABLE_DIRECT_BENCH:-}" == '1' ]]; then
        echo 'si'
    else
        echo 'no / variable ausente (consultar configuracion real del contenedor)'
    fi
} > "$out/entorno.txt"

# Se incluyen fuentes, herramientas, SQL de inicializacion y Compose. La
# huella permite comparar el mismo codigo incluso sin un directorio .git.
{
    find academic_records_db cli core mysql net owner prover verifier ui \
        tests tools third_party -type f \
        \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \
           -o -name '*.js' -o -name '*.css' -o -name '*.html' \
           -o -name '*.py' -o -name '*.sh' -o -name '*.sql' \
           -o -name 'CMakeLists.txt' \) -print0
    printf '%s\0' CMakeLists.txt Dockerfile docker-compose.yml \
        compose.server.yml compose.client.yml
} | sort -zu | xargs -0 -r sha256sum | sha256sum \
    | awk '{print $1}' > "$out/huella_codigo_sha256.txt"

printf 'Entorno %s registrado en: %s\n' "$rol" "$out"
printf 'Revisa entorno.txt antes de compartirlo; contiene direcciones IP y datos del equipo.\n'

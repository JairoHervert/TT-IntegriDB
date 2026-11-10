#!/usr/bin/env python3
"""Comprueba el rechazo de respuestas modificadas sin alterar MySQL.

Ejecutar en la máquina cliente, con el probador accesible por la red.
Utiliza los comandos query y verify del verificador de este proyecto.
"""

import argparse
import csv
import hashlib
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone


REPO = Path(__file__).resolve().parents[2]
VERIFIER = REPO / "build" / "verifier" / "tt_verifier"
QUERY = ("C4", "6", "8")


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", default=os.environ.get("TT_PROVER_ADDR", "127.0.0.1:7070"),
                        help="dirección del probador, host:puerto")
    parser.add_argument("--keys", type=Path, default=REPO / "deploy" / "verifier",
                        help="carpeta con public_key.txt")
    parser.add_argument("--delta", type=Path, default=REPO / "deploy" / "verifier" / "delta.txt",
                        help="digest autenticado del verificador")
    parser.add_argument("--output", type=Path,
                        default=Path.home() / "tt_resultados" /
                        ("seguridad_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")),
                        help="directorio de evidencias")
    parser.add_argument("--timeout", type=int, default=600,
                        help="límite por comando, en segundos")
    return parser.parse_args()


def file_sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run_case(args, case_id, label, words, response, expected_exit, rows, mode="verify"):
    if mode == "query":
        command = [str(VERIFIER), "query", *words, "--server", args.server,
                   "--save", str(response), "--timeout", str(args.timeout)]
    else:
        command = [str(VERIFIER), "verify", *words, "--response", str(response)]
    command.extend(["--keys", str(args.keys), "--delta", str(args.delta), "--show", "0"])
    try:
        execution = subprocess.run(command, cwd=REPO, capture_output=True,
                                   text=True, timeout=args.timeout, check=False)
        return_code = execution.returncode
        output = execution.stdout + "\n" + execution.stderr
    except subprocess.TimeoutExpired as error:
        return_code = 124
        output = f"Se agotó el tiempo de espera: {error}\n"
    (args.output / f"{case_id}.log").write_text(output, encoding="utf-8")
    accepted = return_code == expected_exit
    # El código 3 indica rechazo de la prueba. Un error de ejecución (1)
    # no cuenta como rechazo criptográfico satisfactorio.
    path = response if response.exists() else None
    rows.append({"caso": case_id, "descripcion": label, "consulta": " ".join(words),
                 "salida_esperada": expected_exit, "salida_observada": return_code,
                 "resultado": "OK" if accepted else "FALLO",
                 "archivo": path.name if path else "",
                 "tamano_bytes": path.stat().st_size if path else "",
                 "sha256": file_sha256(path) if path else ""})
    print(f"{case_id}: {'OK' if accepted else 'FALLO'} (salida {return_code}; esperada {expected_exit})")
    return accepted


def main():
    args = arguments()
    if args.timeout < 1:
        raise ValueError("--timeout debe ser positivo")
    if not VERIFIER.is_file():
        print(f"No existe el ejecutable {VERIFIER}; primero compila C++ en el cliente.", file=sys.stderr)
        return 2
    if not args.keys.joinpath("public_key.txt").is_file() or not args.delta.is_file():
        print("Falta la clave pública o el digest del verificador.", file=sys.stderr)
        return 2
    if args.output.exists() and any(args.output.iterdir()):
        print(f"El directorio de evidencias no está vacío: {args.output}", file=sys.stderr)
        return 2
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    original = args.output / "respuesta_original.bin"

    # Primero se conserva una respuesta real obtenida por la red.
    if not run_case(args, "S01", "Respuesta auténtica desde el servidor", QUERY,
                    original, 0, rows, mode="query") or not original.is_file():
        print("Sin respuesta auténtica no se pueden interpretar los casos restantes.", file=sys.stderr)
        write_summary(args.output, rows)
        return 1
    data = original.read_bytes()
    if len(data) < 32:
        print("La respuesta es demasiado corta para los casos de alteración.", file=sys.stderr)
        write_summary(args.output, rows)
        return 1
    run_case(args, "S02", "Respuesta auténtica guardada", QUERY, original, 0, rows)

    # Los casos negativos utilizan copias locales. Nunca modifican el
    # estado de Setup ni las filas almacenadas en MySQL.
    changed = bytearray(data)
    changed[len(changed) // 2] ^= 0x01
    path = args.output / "respuesta_bit_alterado.bin"
    path.write_bytes(changed)
    run_case(args, "S03", "Un bit alterado en la respuesta", QUERY, path, 3, rows)

    path = args.output / "respuesta_truncada.bin"
    path.write_bytes(data[:-1])
    run_case(args, "S04", "Respuesta incompleta", QUERY, path, 3, rows)

    path = args.output / "respuesta_sobrante.bin"
    path.write_bytes(data + b"\x00")
    run_case(args, "S05", "Byte adicional al final", QUERY, path, 3, rows)

    run_case(args, "S06", "Respuesta de C4 6 8 usada para C4 6 9",
             ("C4", "6", "9"), original, 3, rows)
    run_case(args, "S07", "Respuesta de C4 usada para C5",
             ("C5", "20241", "20242"), original, 3, rows)

    changed = bytearray(data)
    changed[9] ^= 0x01
    path = args.output / "respuesta_cabecera_alterada.bin"
    path.write_bytes(changed)
    run_case(args, "S08", "Byte alterado cerca de la cabecera", QUERY, path, 3, rows)

    write_summary(args.output, rows)
    failed = sum(r["resultado"] != "OK" for r in rows)
    print(f"\nCasos satisfactorios: {len(rows) - failed}/{len(rows)}")
    print(f"Evidencias guardadas en: {args.output}")
    return 1 if failed else 0


def write_summary(folder, rows):
    if not rows:
        return
    with (folder / "resumen_seguridad.csv").open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    sys.exit(main())

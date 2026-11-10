#!/usr/bin/env python3
"""Comprueba el resultado de SQL directo frente al resultado verificado.

No mide rendimiento. Es un control previo: una comparacion de tiempos no es
valida si cambian la consulta, la cantidad de filas o los valores devueltos.
Requiere que ambas rutas esten habilitadas y que MySQL no se modifique entre
las dos solicitudes. No exporta valores de la base.
"""

import argparse
import collections
import json
import math
import sys
import urllib.error
import urllib.request


CONSULTAS = {
    "C1": "C1 2020650001", "C2": "C2 1", "C3": "C3 1",
    "C4": "C4 6 8", "C5": "C5 20241 20242", "C6": "C6 20241 20242",
    "C7": "C7 6 8 20241 20242", "C8": "C8", "C9": "C9",
    "C10": "C10 1", "C11": "C11 1", "C12": "C12 1 1",
    "C13": "C13 1", "C14": "C14 1",
}


def consultar(base, ruta, texto, timeout):
    """Ejecuta una sola consulta contra la API local de tt_verifier ui."""
    solicitud = urllib.request.Request(
        base.rstrip("/") + ruta, data=texto.encode("ascii"), method="POST",
        headers={"X-TT-UI": "1", "Content-Type": "text/plain"},
    )
    with urllib.request.urlopen(solicitud, timeout=timeout) as respuesta:
        return json.load(respuesta)


def normalizar(valor):
    """Normaliza numeros para comparar AVG de MySQL con Verify."""
    if isinstance(valor, (int, float)) and not isinstance(valor, bool):
        if not math.isfinite(valor):
            raise ValueError("resultado numerico no finito")
        return ("numero", round(float(valor), 9))
    if valor is None:
        return ("null", "")
    return ("texto", valor)


def equivalentes(a, b, promedio=False):
    """Revisa columnas y filas. AVG puede venir redondeado de MySQL."""
    if a.get("truncated") or b.get("truncated"):
        return False, "alguna respuesta fue truncada; no se puede validar toda la consulta"
    if a.get("columns") != b.get("columns"):
        return False, "distinto esquema/orden de columnas"
    if a.get("row_count") != b.get("row_count"):
        return False, "diferente numero de filas"
    if promedio:
        # MySQL suele entregar AVG de enteros como DECIMAL de escala finita;
        # Verify calcula SUM/COUNT en coma flotante. Evitar un falso fallo.
        left = a.get("rows", [[None]])[0][0]
        right = b.get("rows", [[None]])[0][0]
        if left is None or right is None:
            return (True, "ambos NULL") if left is None and right is None else (False, "NULL distinto")
        igual = math.isclose(left, right, abs_tol=1e-4, rel_tol=0)
        return igual, "AVG equivalente" if igual else "AVG distinto"
    def filas(resultado):
        return collections.Counter(tuple(normalizar(v) for v in row) for row in resultado.get("rows", []))
    return (True, "iguales") if filas(a) == filas(b) else (False, "diferentes valores de las filas")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8080", help="interfaz local")
    parser.add_argument("--all", action="store_true", help="comprueba C1-C14")
    parser.add_argument("--case", choices=CONSULTAS, help="una sola consulta del catalogo")
    parser.add_argument("--timeout", type=int, default=600, help="segundos por solicitud")
    args = parser.parse_args()
    pruebas = (CONSULTAS if args.all else {args.case: CONSULTAS[args.case]}) if args.case else (
        CONSULTAS if args.all else {k: CONSULTAS[k] for k in ("C1", "C4", "C14")})
    ok = True
    for nombre, texto in pruebas.items():
        try:
            seguro = consultar(args.url, "/api/query", texto, args.timeout)
            directo = consultar(args.url, "/api/direct", texto, args.timeout)
            if seguro.get("outcome") != "verified" or directo.get("outcome") != "direct" or not directo.get("unverified"):
                print(f"{nombre}: NO COMPARABLE (Verify={seguro.get('outcome')}, SQL={directo.get('outcome')})")
                ok = False
                continue
            igual, motivo = equivalentes(seguro["result"], directo["result"], nombre == "C10")
            print(f"{nombre}: {'OK' if igual else 'DIFERENTE'} ({motivo}; {seguro['result']['row_count']} filas)")
            ok = ok and igual
        except (urllib.error.URLError, json.JSONDecodeError, KeyError, ValueError, TypeError) as exc:
            print(f"{nombre}: ERROR ({exc})")
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

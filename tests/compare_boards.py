#!/usr/bin/env python3
"""Compara dos volcados de tablero producidos por tests/dump_board.cpp
(o por cualquier binario que respete el mismo formato de salida) y decide
si son equivalentes segun docs/TESTING.md, seccion 3:

  1. mismo tamano de tablero (ancho x alto);
  2. mismo numero de generaciones ejecutadas;
  3. mismo tamano final del tablero (redundante con 1, verificado igual);
  4. cada celda con el mismo estado;
  5. el mismo numero de celulas vivas.

Este script no ejecuta ningun motor: solo compara dos archivos de texto ya
generados. Eso permite usarlo hoy (dos volcados del motor secuencial) y,
sin cambios, el dia que existan binarios OpenMP/Pthreads/MPI que produzcan
un volcado con este mismo formato.

Formato esperado de cada archivo (ver tests/dump_board.cpp):
    linea 1: "<ancho> <alto> <generacion> <vivas>"
    lineas siguientes: <alto> filas de <ancho> caracteres '0'/'1'

Uso:
    python3 compare_boards.py referencia.txt candidata.txt
    python3 compare_boards.py --require-generation 4 referencia.txt candidata.txt

Codigo de salida: 0 si son equivalentes, 1 si no lo son, 2 ante un error de
formato o de uso.
"""

import argparse
import sys
from dataclasses import dataclass


@dataclass
class Board:
    width: int
    height: int
    generation: int
    alive: int
    rows: list


def parse_board(path: str) -> Board:
    with open(path, "r", encoding="utf-8") as handle:
        lines = [line.rstrip("\n") for line in handle]

    if not lines:
        raise ValueError(f"{path}: archivo vacio")

    header = lines[0].split()
    if len(header) != 4:
        raise ValueError(
            f"{path}: encabezado invalido (se esperaban 4 campos: "
            f"ancho alto generacion vivas)"
        )

    try:
        width, height, generation, alive = (int(field) for field in header)
    except ValueError as exc:
        raise ValueError(f"{path}: encabezado con valores no numericos") from exc

    rows = lines[1:]
    if len(rows) != height:
        raise ValueError(
            f"{path}: se esperaban {height} filas y se encontraron {len(rows)}"
        )

    for index, row in enumerate(rows):
        if len(row) != width:
            raise ValueError(
                f"{path}: la fila {index} tiene longitud {len(row)}, "
                f"se esperaba {width}"
            )
        if any(ch not in "01" for ch in row):
            raise ValueError(f"{path}: la fila {index} contiene caracteres invalidos")

    return Board(width=width, height=height, generation=generation, alive=alive, rows=rows)


def compare(reference: Board, candidate: Board) -> list:
    """Devuelve la lista de motivos de no equivalencia (vacia si son
    equivalentes). Cada motivo corresponde a uno de los criterios de
    docs/TESTING.md, seccion 3."""

    reasons = []

    if (reference.width, reference.height) != (candidate.width, candidate.height):
        reasons.append(
            "tamano de tablero distinto: "
            f"referencia={reference.width}x{reference.height}, "
            f"candidata={candidate.width}x{candidate.height}"
        )
        # Sin el mismo tamano no tiene sentido comparar celda a celda.
        return reasons

    if reference.generation != candidate.generation:
        reasons.append(
            "numero de generaciones distinto: "
            f"referencia={reference.generation}, candidata={candidate.generation}"
        )

    cell_mismatches = []
    for row in range(reference.height):
        for col in range(reference.width):
            if reference.rows[row][col] != candidate.rows[row][col]:
                cell_mismatches.append((row, col))

    if cell_mismatches:
        preview = ", ".join(f"({r},{c})" for r, c in cell_mismatches[:10])
        more = "" if len(cell_mismatches) <= 10 else f" (+{len(cell_mismatches) - 10} mas)"
        reasons.append(
            f"{len(cell_mismatches)} celda(s) distinta(s): {preview}{more}"
        )

    if reference.alive != candidate.alive:
        reasons.append(
            "conteo de celulas vivas distinto: "
            f"referencia={reference.alive}, candidata={candidate.alive}"
        )

    return reasons


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference", help="Volcado de referencia (normalmente el motor secuencial)")
    parser.add_argument("candidate", help="Volcado a validar (por ejemplo, OpenMP/Pthreads/MPI)")
    parser.add_argument(
        "--require-generation",
        type=int,
        default=None,
        help="Si se indica, ademas exige que ambos volcados esten en esta generacion exacta",
    )
    args = parser.parse_args()

    try:
        reference = parse_board(args.reference)
        candidate = parse_board(args.candidate)
    except ValueError as error:
        print(f"Error de formato: {error}", file=sys.stderr)
        return 2

    if args.require_generation is not None:
        for label, board in (("referencia", reference), ("candidata", candidate)):
            if board.generation != args.require_generation:
                print(
                    f"Error: la {label} esta en la generacion {board.generation}, "
                    f"se esperaba {args.require_generation}",
                    file=sys.stderr,
                )
                return 2

    reasons = compare(reference, candidate)

    if not reasons:
        print(
            f"EQUIVALENTE: {reference.width}x{reference.height}, "
            f"generacion {reference.generation}, {reference.alive} vivas"
        )
        return 0

    print("NO EQUIVALENTE:")
    for reason in reasons:
        print(f"  - {reason}")
    return 1


if __name__ == "__main__":
    sys.exit(main())

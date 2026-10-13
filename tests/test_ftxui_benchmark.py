"""Navegación Benchmark sobre PTY, sin umbrales de rendimiento."""
import os
from pathlib import Path
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_menu import expect_focus, expect_identity_heading


def visible(terminal, *values):
    terminal.assert_fits()
    text = "\n".join(terminal.screen().values())
    for value in values:
        assert value in text, (value, text)


def wait_result(terminal):
    # Límite de bloqueo del proceso, nunca un umbral de rendimiento.
    deadline = time.monotonic() + 90
    while b"Benchmark finalizado." not in terminal.raw:
        assert terminal.process.poll() is None
        assert time.monotonic() < deadline, "El worker no publicó su resultado"
        terminal.read()


def check(binary, cwd, columns, rows):
    terminal = Terminal(binary, cwd, columns, rows)
    try:
        identity = terminal.identity()
        terminal.send("\r")
        expect_identity_heading(terminal, "BENCHMARK")
        expect_focus(terminal, "Comparación de motores")
        visible(terminal, "Compara los motores Secuencial y OpenMP")
        assert "Ejecutando mediciones" not in terminal.raw.decode(errors="replace")
        # Sin patrón, la comparación conserva el acceso al catálogo.
        terminal.send("\r")
        expect_identity_heading(terminal, "CATÁLOGO DE PATRONES")
        terminal.send("\x1b")
        expect_focus(terminal, "Comparación de motores")
        terminal.send("\r")
        expect_identity_heading(terminal, "CATÁLOGO DE PATRONES")
        terminal.send("\r")
        wait_result(terminal)
        visible(terminal, "Mediana secuencial:", "Mediana OpenMP:", "Equivalencia:")
        terminal.expect("Patrón: cedar.txt", "3 × 7")
        before = terminal.raw
        terminal.send("\r" * 12)
        assert terminal.raw == before, "Enter duplicó la medición o abandonó el reporte"
        terminal.send("\x1b")
        expect_focus(terminal, "Comparación de motores")
        terminal.send("\x1b[B")
        expect_focus(terminal, "Escalabilidad OpenMP")
        terminal.send("\r")
        expect_identity_heading(terminal, "ESCALABILIDAD OPENMP")
        visible(terminal, "próximas", "iteraciones")
        terminal.send("\r" * 3)
        expect_identity_heading(terminal, "ESCALABILIDAD OPENMP")
        terminal.send("\x1b")
        expect_focus(terminal, "Escalabilidad OpenMP")
        terminal.send("\x1b[B")
        expect_focus(terminal, "Resultados de la sesión")
        terminal.send("\r")
        expect_identity_heading(terminal, "RESULTADOS DE LA SESIÓN")
        visible(terminal, "El historial de resultados de la sesión")
        terminal.send("\x1b")
        expect_focus(terminal, "Resultados de la sesión")
        terminal.send("\x1b")
        expect_focus(terminal, "Benchmark")
        terminal.send("1")
        expect_focus(terminal, "Resultados de la sesión")
        terminal.send("\x1b[A")
        expect_focus(terminal, "Escalabilidad OpenMP")
        assert terminal.identity() == identity
        assert b"\x1b[2J" not in terminal.raw
        before = terminal.raw
        terminal.read()
        assert terminal.raw == before, "Redibujado periódico en pantalla inactiva"
        terminal.send("\x1b")
        terminal.send("q")
        assert terminal.process.wait(timeout=5) == 0
        print(f"PASS: rutas Benchmark, foco RGB y comparación {columns}x{rows}")
    finally:
        terminal.close()


def check_worker(binary, cwd):
    terminal = Terminal(binary, cwd, 120, 40)
    try:
        terminal.send("C")
        terminal.send("m")
        terminal.send(str(Path(cwd) / "large.txt") + "\r")
        terminal.send("1")
        # Enviar sin la espera de send(): observar el primer frame de trabajo.
        os.write(terminal.master, b"\r")
        deadline = time.monotonic() + 10
        while "Ejecutando mediciones" not in terminal.raw.decode(errors="replace"):
            assert time.monotonic() < deadline
            terminal.read()
        terminal.send("\r" * 20)
        terminal.send("\x1b")
        expect_focus(terminal, "Comparación de motores")
        terminal.send("\x1b[B\r")
        expect_identity_heading(terminal, "ESCALABILIDAD OPENMP")
        wait_result(terminal)
        assert terminal.raw.decode(errors="replace").count("Ejecutando mediciones") == 1
        # La publicación no fuerza la página ni pierde el foco del submenú.
        expect_identity_heading(terminal, "ESCALABILIDAD OPENMP")
        terminal.send("\x1b")
        expect_focus(terminal, "Escalabilidad OpenMP")
        terminal.send("\x1b")
        terminal.send("q")
        assert terminal.process.wait(timeout=5) == 0
        assert b"\x1b[2J" not in terminal.raw
        print("PASS: Enter repetido, navegación y publicación del worker")
    finally:
        terminal.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    # Mantener el coste de OpenMP acotado sin condicionar tiempos medidos.
    os.environ["OMP_THREAD_LIMIT"] = "2"
    with tempfile.TemporaryDirectory(prefix="conway-benchmark-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "cedar.txt").write_text("3 7\n" + "0000000\n" * 3)
        (Path(cwd) / "large.txt").write_text("2000 2000\n" + ("0" * 2000 + "\n") * 2000)
        for size in [(120, 40), (80, 24)]:
            check(binary, cwd, *size)
        check_worker(binary, cwd)

"""Transiciones reales de tamaño: conservar vistas, sesión, foco y resultados."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import sys
import tempfile
import termios
import time
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_benchmark import visible, field, execute, wait_state, focused, move_to

SIZES = [(160, 45), (120, 40), (100, 30), (80, 24)]
ESC = "\x1b"


def resize(t, columns, rows):
    if (t.columns, t.rows) == (columns, rows):
        t.assert_fits()
        return
    t.columns, t.rows = columns, rows
    t.raw = b""
    fcntl.ioctl(t.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
    os.kill(t.process.pid, signal.SIGWINCH)
    t.read()
    t.assert_fits()


def text(t):
    return " ".join(t.screen().values())


def check(binary, cwd, size):
    t = Terminal(binary, cwd, *size)
    try:
        wide = size[0] >= 120 and size[1] >= 40
        t.send("C")
        visible(t, "wide")
        t.send("\x1b[B")
        resize(t, 80, 24)
        focused(t, "zzz-small")
        resize(t, *size)
        t.send("\x1b[A")
        if not wide:
            assert "ACTUAL ·" not in text(t)
            t.send("d")
            visible(t, "ACTUAL · generación 0")
            t.send("v")
            visible(t, "SIGUIENTE · generación 1")
            t.send("d")
        t.send("\r2")
        visible(t, "CURRENT · generación 0")
        if not wide:
            assert "NEXT · generación 1" not in text(t)
            t.send("v")
            visible(t, "NEXT · generación 1")
        t.send("n")
        resize(t, 160, 45)
        visible(t, "CURRENT · generación 1", "NEXT · generación 2")
        resize(t, 80, 24)
        t.send("\x1b[B\x1b[C")
        visible(t, "filas 2")
        before = t.raw
        t.read()
        assert before == t.raw, "Refresco periódico de simulación pausada"
        resize(t, 32, 12)
        visible(t, "Terminal insuficiente")
        resize(t, 80, 24)
        visible(t, "generación 1" if wide else "generación 2")
        t.send(ESC)
        t.send("\x1b[B" * 5 + "\r")
        deadline = time.monotonic() + 30
        while "Preparando generación" in text(t):
            assert time.monotonic() < deadline, text(t)
            # El controlador existente publica el resultado al siguiente evento UI.
            t.send("\x1b[D")
        visible(t, "MODO DIDÁCTICO", "NEXT · generación 1")
        t.send("v")
        visible(t, "CURRENT · generación 0")
        t.send("t")
        visible(t, "TRABAJO POR HILO")
        resize(t, 160, 45)
        visible(t, "TRABAJO POR HILO")
        resize(t, 80, 24)
        visible(t, "TRABAJO POR HILO")
        t.send("t")
        visible(t, "CURRENT · generación 0")
        t.send(ESC)
        resize(t, 40, 18)
        visible(t, "MENÚ PRINCIPAL")
        print(f"PASS: catálogo, simulación, didáctica y resize {size}")
    finally:
        t.close()


def experiments(binary, cwd):
    t = Terminal(binary, cwd, 80, 24)
    try:
        t.send("C\r1\r")
        for scaling in (False, True):
            field(t, "Generaciones", "2")
            field(t, "Calentamientos", "0")
            field(t, "Repeticiones", "1")
            field(t, "Lista de hilos" if scaling else "Hilos OpenMP", "1,2" if scaling else "2")
            field(t, "Generaciones", "2")
            for size in SIZES:
                resize(t, *size)
                focused(t, "Generaciones", "2")
            execute(t)
            wait_state(t, "Finalizado")
            for size in SIZES:
                resize(t, *size)
                visible(t, "IDENTIFICACIÓN", f"Ejecución #{2 if scaling else 1}")
                t.send("\x1b[C")
                visible(t, "ESTADÍSTICAS")
                t.send("\x1b[F")
                t.assert_fits()
                t.send("\x1b[D")
            t.send(ESC)
            move_to(t, "Generaciones")
            focused(t, "Generaciones", "2")
            if not scaling:
                t.send(ESC)
                t.send("\x1b[B\r")
        print("PASS: formularios, comparación y escalabilidad en los cuatro tamaños")
    finally:
        t.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="conway-responsive-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "wide.txt").write_text("17 31\n" + "010" * 10 + "0\n" + ("0" * 31 + "\n") * 16)
        (patterns / "zzz-small.txt").write_text("1 1\n1\n")
        for size in SIZES:
            check(binary, cwd, size)
        experiments(binary, cwd)

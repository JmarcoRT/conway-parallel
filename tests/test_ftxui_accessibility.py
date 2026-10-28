"""Accesibilidad de cada extremo real del tablero y del último trabajador."""
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_responsive import resize
from test_ftxui_benchmark import visible, focused

SIZES = [(160, 45), (120, 40), (100, 30), (80, 24)]
UP, DOWN, LEFT, RIGHT = "\x1b[A", "\x1b[B", "\x1b[D", "\x1b[C"
RANGE = re.compile(r"Viewport: filas (\d+)–(\d+)/(\d+) · col\. (\d+)–(\d+)/(\d+)")


def screen(t):
    return "\n".join(t.screen().values())


def viewport(t, paired):
    t.assert_fits()
    text = screen(t)
    match = RANGE.search(text)
    assert match, text
    values = tuple(map(int, match.groups()))
    assert 1 <= values[0] <= values[1] <= values[2], values
    assert 1 <= values[3] <= values[4] <= values[5], values
    # The actual grid must have both borders and all advertised logical rows.
    assert text.count("┌──") == (2 if paired else 1), text
    assert text.count("└──") == (2 if paired else 1), text
    cells = sum(bool(re.search(r"│(?:  │){2}", row)) for row in t.screen().values())
    assert cells == values[1] - values[0] + 1, (cells, values, text)
    return values


def endpoints(t, paired):
    t.send(UP * 100 + LEFT * 100)
    start = viewport(t, paired)
    assert start[0] == start[3] == 1, start
    t.send(DOWN * 100 + RIGHT * 100)
    end = viewport(t, paired)
    assert end[1] == end[2] and end[4] == end[5], end
    return end


def ready(t):
    deadline = time.monotonic() + 30
    while "Preparando generación" in screen(t):
        assert time.monotonic() < deadline, screen(t)
        t.send(LEFT)  # Esperar la notificación de captura asíncrona.
    visible(t, "MODO DIDÁCTICO")


def preserved(old, new):
    assert new[0] == min(old[0], new[2] - (new[1] - new[0] + 1) + 1), (old, new)
    assert new[3] == min(old[3], new[5] - (new[4] - new[3] + 1) + 1), (old, new)


def check(binary, cwd, size, name):
    paired = size[0] >= 120 and size[1] >= 40
    t = Terminal(binary, cwd, *size)
    try:
        t.send("4c12\r3oC")
        if name == "pulsar":
            t.send(DOWN)
        focused(t, name)
        t.send("d")
        visible(t, "DETALLE ACTIVO", "Patrón: " + name)
        endpoints(t, paired)
        visible(t, "Patrón: " + name)
        before = screen(t)
        if paired:
            t.send("v")
            assert screen(t) == before, "V alteró la vista doble del catálogo"
        else:
            t.send("v")
            visible(t, "SIGUIENTE · generación 1")
            t.send("v")
        old = viewport(t, paired)
        target = (80, 24) if paired else (160, 45)
        resize(t, *target)
        preserved(old, viewport(t, not paired))
        visible(t, "DETALLE ACTIVO", "Patrón: " + name)
        if paired:
            visible(t, "ACTUAL · generación 0")  # V amplio no cambió selección oculta.
        resize(t, *size)
        endpoints(t, paired)
        t.send("d")
        focused(t, name)
        # La navegación de lista sigue funcionando después de mover el detalle.
        t.send(UP if name == "pulsar" else DOWN)
        focused(t, "gosper_glider_gun" if name == "pulsar" else "pulsar")
        t.send(DOWN if name == "pulsar" else UP)
        focused(t, name)
        t.send("\r")
        t.expect("Patrón: " + name + ".txt")
        t.send("2")
        if paired:
            before = screen(t)
            t.send("v")
            assert screen(t) == before, "V alteró la simulación doble"
            resize(t, 80, 24)
            visible(t, "CURRENT · generación 0")
        t.send("v")
        visible(t, "NEXT · generación 1")
        resize(t, 160, 45)
        before = screen(t)
        t.send("v")
        assert screen(t) == before, "V cambió una selección oculta"
        resize(t, 80, 24)
        visible(t, "NEXT · generación 1")
        resize(t, *size)
        t.send("\x1b")
        t.send(DOWN * 5 + "\r")
        ready(t)
        t.send(" ")  # Playback now starts automatically; freeze for viewport assertions.
        visible(t, "efectivos 12")
        old = endpoints(t, paired)
        before = screen(t)
        if paired:
            t.send("v")
            assert screen(t) == before, "V alteró la vista doble didáctica"
        else:
            t.send("v")
            visible(t, "CURRENT · generación 0")
            t.send("v")
        t.send("t")
        visible(t, "TRABAJO POR HILO")
        t.send(DOWN * 100)
        visible(t, "T11 · filas", "celdas", "ms", "%", "pendiente")
        resize(t, *target)
        visible(t, "T11 · filas")
        t.send("t")
        preserved(old, viewport(t, not paired))
        visible(t, "Generación actual 0")
        if paired:
            visible(t, "NEXT · generación 1")
        resize(t, *size)
        endpoints(t, paired)
        before = t.raw
        t.read()
        assert before == t.raw, "Refrescos periódicos estando en pausa"
        print(f"PASS {name} {size[0]}x{size[1]}: extremos, bordes, foco, V, resize y T11", flush=True)
    finally:
        t.close()


if __name__ == "__main__":
    os.environ.update(OMP_DYNAMIC="FALSE", OMP_THREAD_LIMIT="12")
    binary = str(Path(sys.argv[1]).resolve())
    repo = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="conway-accessibility-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        for name in ("gosper_glider_gun", "pulsar"):
            shutil.copy(repo / "patterns" / (name + ".txt"), patterns)
        for size in SIZES:
            for name in ("gosper_glider_gun", "pulsar"):
                check(binary, cwd, size, name)

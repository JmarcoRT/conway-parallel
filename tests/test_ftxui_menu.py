"""Inspecciona RGB, ancho y foco de menus reales sobre una PTY."""
from pathlib import Path
import re
import sys
import tempfile
sys.dont_write_bytecode = True
from test_ftxui_header import ANSI, ROW, Terminal

CYAN = (83, 191, 208)
CHARCOAL = (25, 32, 38)
SGR = re.compile(r"\x1b\[([0-9;]*)m")


def pixels(row):
    foreground = background = None
    result = []
    start = 0
    for match in SGR.finditer(row):
        result.extend((char, foreground, background) for char in row[start:match.start()])
        codes = [int(value or 0) for value in match[1].split(";")]
        index = 0
        while index < len(codes):
            code = codes[index]
            if code == 0:
                foreground = background = None
            elif code in (38, 48) and codes[index + 1:index + 2] == [2]:
                color = tuple(codes[index + 2:index + 5])
                if code == 38:
                    foreground = color
                else:
                    background = color
                index += 4
            elif code == 39:
                foreground = None
            elif code == 49:
                background = None
            index += 1
        start = match.end()
    result.extend((char, foreground, background) for char in row[start:])
    return result


def expect_focus(terminal, label, full_width=True):
    rows = {}
    for match in ROW.finditer(terminal.raw.decode("utf-8", errors="replace")):
        rows[int(match[1])] = match[2]
    matches = [row for row in rows.values() if "> " + label in ANSI.sub("", row)]
    assert len(matches) == 1, (label, terminal.screen())
    row_pixels = pixels(matches[0])
    focused = [(index, pixel) for index, pixel in enumerate(row_pixels) if pixel[2] == CYAN]
    assert focused, (label, matches[0])
    assert all(pixel[1] == CHARCOAL for _, pixel in focused), (label, focused)
    assert len(focused) > len(label) + 2, (label, focused)  # Incluye relleno.
    if full_width:
        assert len(focused) == terminal.columns - 2, (label, len(focused))
    # Un unico menu conserva el foco; las selecciones inactivas no son cian.
    colored_rows = [row for row in rows.values() if any(pixel[2] == CYAN for pixel in pixels(row))]
    assert len(colored_rows) == 1, (label, len(colored_rows))


def expect_identity_heading(terminal, label):
    rows = {}
    for match in ROW.finditer(terminal.raw.decode("utf-8", errors="replace")):
        rows[int(match[1])] = match[2]
    for row in rows.values():
        row_pixels = pixels(row)
        text = "".join(pixel[0] for pixel in row_pixels)
        if label in text:
            start = text.index(label)
            assert all(pixel[1] == CYAN for pixel in row_pixels[start:start + len(label)]), row
            return
    raise AssertionError((label, terminal.screen()))


def check(binary, cwd, columns, rows):
    terminal = Terminal(binary, cwd, columns, rows)
    try:
        expect_focus(terminal, "Benchmark")
        expect_identity_heading(terminal, "MENÚ PRINCIPAL")
        terminal.send("\x1b[B")
        expect_focus(terminal, "Visualización")
        terminal.send("3")
        expect_focus(terminal, "Secuencial")
        expect_identity_heading(terminal, "MOTOR DE EJECUCIÓN")
        terminal.send("\x1b[B")
        expect_focus(terminal, "OpenMP")
        terminal.send("\r")
        terminal.expect("Motor: OpenMP")
        terminal.send("4")
        expect_focus(terminal, "1 hilo")
        expect_identity_heading(terminal, "CONFIGURACIÓN OPENMP")
        terminal.send("\x1b[B")
        expect_focus(terminal, "2 hilos")
        terminal.send("\r")
        terminal.expect("OpenMP solicitado: 2")
        if columns < 64:
            # El catalogo vertical tiene una limitacion previa de espacio;
            # aqui se verifican los menus principal, motores e hilos.
            terminal.send("q")
            assert terminal.process.wait(timeout=3) == 0
            print(f"PASS: resaltado RGB, ancho y foco de menus {columns}x{rows}")
            return
        terminal.send("C")
        expect_focus(terminal, "cedar", False)
        expect_identity_heading(terminal, "CATÁLOGO DE PATRONES")
        terminal.send("\x1b[B")
        expect_focus(terminal, "willow", False)
        terminal.send("\x1b[B")  # Transfiere foco a ACCIONES.
        expect_focus(terminal, "Ruta manual...", False)
        terminal.send("\x1b[B")
        expect_focus(terminal, "Volver", False)
        terminal.send("\x1b[A")
        terminal.send("\x1b[A")  # Vuelve a la lista, manteniendo seleccion.
        expect_focus(terminal, "willow", False)
        terminal.send("\r")
        terminal.expect("Patrón: willow.txt", "6 × 4")
        expect_focus(terminal, "Visualización")
        terminal.send("q")
        assert terminal.process.wait(timeout=3) == 0
        print(f"PASS: resaltado RGB, ancho y foco {columns}x{rows}")
    finally:
        terminal.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="conway-menu-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "cedar.txt").write_text("3 7\n" + "0000000\n" * 3)
        (patterns / "willow.txt").write_text("6 4\n" + "0000\n" * 6)
        for size in [(160, 50), (120, 40), (80, 24), (50, 24)]:
            check(binary, cwd, *size)

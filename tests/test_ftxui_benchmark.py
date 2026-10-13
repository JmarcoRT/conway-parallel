"""Formularios experimentales reales en PTY: edición, validación y foco."""
from pathlib import Path
import sys
import tempfile
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal, ROW
from test_ftxui_menu import expect_focus, expect_identity_heading, pixels, CYAN, CHARCOAL

TAB = "\t"
BACK_TAB = "\x1b[Z"
ESC = "\x1b"


def visible(terminal, *values):
    terminal.assert_fits()
    text = " ".join(line.strip("│ ") for line in terminal.screen().values())
    for value in values:
        assert value in text, (value, text)


def focus_text(terminal):
    rows = {}
    for match in ROW.finditer(terminal.raw.decode(errors="replace")):
        rows[int(match[1])] = match[2]
    highlighted = [[p for p in pixels(row) if p[2] == CYAN] for row in rows.values()]
    highlighted = [row for row in highlighted if row]
    assert len(highlighted) == 1, terminal.screen()
    assert all(p[1] == CHARCOAL for p in highlighted[0]), highlighted
    text = "".join(p[0] for p in highlighted[0])
    terminal.assert_fits()
    return text


def focused(terminal, *values):
    text = focus_text(terminal)
    for value in values:
        assert value in text, (value, text)


def move_to(terminal, label):
    for _ in range(12):
        if label in focus_text(terminal):
            return
        terminal.send(TAB)
    raise AssertionError((label, terminal.screen()))


def edit(terminal, value):
    terminal.send("\x1b[H" + "\x1b[3~" * 40 + value)


def check(binary, cwd, columns, rows):
    terminal = Terminal(binary, cwd, columns, rows)
    try:
        identity = terminal.identity()
        terminal.send("4c3\r")  # Configurar antes de abrir el formulario por primera vez.
        terminal.send("\r\r")
        expect_identity_heading(terminal, "COMPARACIÓN DE MOTORES")
        visible(terminal, "Sin patrón cargado")
        move_to(terminal, "Hilos OpenMP")
        focused(terminal, "Hilos OpenMP", "3")
        move_to(terminal, "Tablero generado")
        # Seleccionar patrón inexistente y validar: error concreto en Entrada.
        terminal.send("\x1b[H\r" + BACK_TAB + "\r")
        focused(terminal, "Patrón cargado")
        visible(terminal, "Sin patrón cargado.")
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send("C\r")
        terminal.send("1\r")
        visible(terminal, "cedar.txt")
        assert "Sin patrón cargado." not in " ".join(terminal.screen().values())
        terminal.send("\x1b[F\r" + TAB)
        focused(terminal, "Ancho", "1000")
        edit(terminal, "0")
        terminal.send(BACK_TAB * 2 + "\r")
        focused(terminal, "Ancho")
        visible(terminal, "Ancho entero positivo")
        edit(terminal, "17")
        terminal.send(TAB)
        edit(terminal, "9")
        invalid = ["30.001", "4294967296", "0", "-1", "0", "0"]
        labels = ["Densidad (%)", "Semilla", "Generaciones", "Calentamientos", "Repeticiones", "Hilos OpenMP"]
        for value in invalid:
            terminal.send(TAB)
            edit(terminal, value)
        terminal.send(TAB * 2 + "\r")
        corrections = ["30.25", "4294967295", "23", "0", "7", "123"]
        errors = ["precisión exacta", "Semilla entera", "Entero entre 1", "Entero entre 0", "Entero entre 1", "Entero entre 1"]
        for index, (label, value, error) in enumerate(zip(labels, corrections, errors)):
            focused(terminal, label)
            visible(terminal, error)
            edit(terminal, value)
            if index < len(labels) - 1:
                terminal.send(TAB)
        terminal.send(TAB * 2 + "\r")
        focused(terminal, "Ejecutar experimento")
        visible(terminal, "Configuración válida.", "La ejecución se habilitará en la siguiente fase")
        terminal.send("\r" * 8)
        visible(terminal, "Configuración válida.")
        terminal.send(ESC)
        expect_focus(terminal, "Comparación de motores")
        terminal.send("\r" + TAB * 2)  # Desde Ejecutar: Entrada, Ancho.
        focused(terminal, "Ancho", "17")
        # Comprobar cada valor conservado, incluyendo los que quedaron fuera de pantalla.
        saved = ["9"] + corrections
        for value in saved:
            terminal.send(TAB)
            focused(terminal, value)
        terminal.send(TAB + "\r")
        focused(terminal, "Restaurar valores estándar")
        terminal.send(TAB * 2 + "\x1b[F\r" + TAB)  # Ejecutar, Entrada, generado, Ancho.
        focused(terminal, "Ancho", "1000")
        move_to(terminal, "Hilos OpenMP")
        focused(terminal, "Hilos OpenMP", "3")
        terminal.send(ESC)
        terminal.send("\x1b[B\r")
        expect_identity_heading(terminal, "ESCALABILIDAD OPENMP")
        terminal.send("\x1b[F\r")  # Elegir generado también en escalabilidad.
        terminal.send(BACK_TAB * 3)  # Desde Entrada: Ejecutar, Restaurar, Lista.
        focused(terminal, "Lista de hilos", "1, 2, 4, 8")
        edit(terminal, "1, 2, 1")
        terminal.send(TAB * 2 + "\r")
        focused(terminal, "Lista de hilos")
        visible(terminal, "hilos duplicados")
        edit(terminal, "8, 1, 3")
        terminal.send(TAB * 2 + "\r")
        visible(terminal, "Configuración válida.")
        terminal.send(ESC)
        expect_focus(terminal, "Escalabilidad OpenMP")
        terminal.send("\r" + BACK_TAB * 2)
        focused(terminal, "Lista de hilos", "8, 1, 3")
        terminal.send(TAB + "\r" + BACK_TAB)
        focused(terminal, "Lista de hilos", "1, 2, 4, 8")
        terminal.send(ESC)
        terminal.send(ESC)
        # El patrón de la aplicación sigue siendo el origen vivo de los formularios.
        terminal.send("C\r")
        terminal.send("1\x1b[A\r")
        move_to(terminal, "Restaurar valores estándar")
        terminal.send("\r")
        move_to(terminal, "Ejecutar experimento")
        terminal.send("\r")
        visible(terminal, "Configuración válida.")
        terminal.send(TAB)  # Entrada.
        terminal.send("\x1b[H\r")
        visible(terminal, "cedar.txt", "7 × 3")
        terminal.send(TAB)
        focused(terminal, "Generaciones", "20")  # Campos generados ocultos del foco.
        terminal.expect("Patrón: cedar.txt", "3 × 7")
        terminal.send(ESC)
        terminal.send("\x1b[B\x1b[B\r")
        expect_identity_heading(terminal, "RESULTADOS DE LA SESIÓN")
        visible(terminal, "El historial de resultados")
        assert terminal.identity() == identity
        raw = terminal.raw.decode(errors="replace")
        assert not any(value in raw for value in ("Ejecutando mediciones", "Mediana secuencial:", "Benchmark finalizado."))
        assert b"\x1b[2J" not in terminal.raw
        before = terminal.raw
        terminal.read()
        assert terminal.raw == before, "Repintado periódico inesperado"
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send("q")
        assert terminal.process.wait(timeout=5) == 0
        print(f"PASS: formularios, edición, errores, restauración, foco y no ejecución {columns}x{rows}")
    finally:
        terminal.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="conway-forms-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "cedar.txt").write_text("3 7\n" + "0000000\n" * 3)
        for size in [(120, 40), (80, 24)]:
            check(binary, cwd, *size)

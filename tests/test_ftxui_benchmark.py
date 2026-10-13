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


def form_text(terminal):
    # Excluir la barra contextual y el pie global: siguen mostrando el patrón de la aplicación.
    return " ".join(line.strip("│ ") for row, line in terminal.screen().items()
                    if terminal.identity_end() + 1 < row <= terminal.rows - 3)


def check_sources(terminal, values, change=False):
    move_to(terminal, "Tablero generado")
    terminal.send("\x1b[F\r")
    text = form_text(terminal)
    assert not any(value in text for value in ("cedar.txt", "7 × 3", "Sin patrón cargado")), text
    if change:
        for value in values:
            terminal.send(TAB)
            edit(terminal, value)
        move_to(terminal, "Tablero generado")
    terminal.send("\x1b[H\r")
    text = form_text(terminal)
    assert "cedar.txt" in text and "7 × 3" in text, text
    assert not any(label in text for label in ("Ancho:", "Alto:", "Densidad (%):", "Semilla:")), text
    terminal.send("\x1b[F\r")
    text = form_text(terminal)
    assert "cedar.txt" not in text and "7 × 3" not in text, text
    for label, value in zip(("Ancho", "Alto", "Densidad (%)", "Semilla"), values):
        terminal.send(TAB)
        focused(terminal, label, value)
    terminal.expect("Patrón: cedar.txt", "3 × 7")


def wait_state(terminal, state):
    import time
    deadline = time.monotonic() + 90  # Límite de bloqueo, no umbral de rendimiento.
    while "Estado: " + state not in " ".join(terminal.screen().values()):
        assert terminal.process.poll() is None
        assert time.monotonic() < deadline, terminal.screen()
        terminal.read()
    terminal.assert_fits()


def field(terminal, label, value):
    move_to(terminal, label)
    edit(terminal, value)


def execute(terminal):
    move_to(terminal, "Ejecutar experimento")
    terminal.send("\r" * 12)


def check(binary, cwd, columns, rows):
    terminal = Terminal(binary, cwd, columns, rows)
    try:
        identity = terminal.identity()
        terminal.send("4c2\rC\r1\r")
        expect_identity_heading(terminal, "COMPARACIÓN DE MOTORES")
        visible(terminal, "cedar.txt", "7 × 3")
        assert "Ancho:" not in form_text(terminal)
        field(terminal, "Generaciones", "0")
        execute(terminal)
        focused(terminal, "Generaciones")
        visible(terminal, "Entero entre 1")
        edit(terminal, "3")
        field(terminal, "Calentamientos", "0")
        field(terminal, "Repeticiones", "2")
        field(terminal, "Hilos OpenMP", "2")
        execute(terminal)
        wait_state(terminal, "Finalizado")
        visible(terminal, "Ejecución #1", "Entrada: cedar.txt", "Equivalencia: SI", "Validez: SI", "Mediana secuencial:")
        before = terminal.raw
        terminal.send("\r" * 12)
        assert terminal.raw == before, "Enter duplicó el experimento finalizado"
        terminal.send(ESC)
        expect_identity_heading(terminal, "COMPARACIÓN DE MOTORES")
        terminal.send(ESC)
        terminal.send("\x1b[B\r")
        expect_identity_heading(terminal, "ESCALABILIDAD OPENMP")
        terminal.send("\x1b[F\r")
        check_sources(terminal, ["7", "5", "30", "42"], change=True)
        field(terminal, "Generaciones", "3")
        field(terminal, "Calentamientos", "0")
        field(terminal, "Repeticiones", "2")
        field(terminal, "Lista de hilos", "1,1")
        execute(terminal)
        focused(terminal, "Lista de hilos")
        visible(terminal, "hilos duplicados")
        edit(terminal, "1,2")
        execute(terminal)
        wait_state(terminal, "Finalizado")
        visible(terminal, "Ejecución #2", "Entrada: Tablero generado", "7 × 5")
        terminal.send("\x1b[F")
        visible(terminal, "OpenMP · 2 hilos solicitados", "Equivalencia: SI", "Contexto conservado:")
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send("\x1b[A\r")
        # Preparación rechazada por presupuesto total: formulario válido, sin reservar el tablero.
        move_to(terminal, "Patrón cargado")
        terminal.send("\x1b[F\r")
        field(terminal, "Ancho", "8000")
        field(terminal, "Alto", "8000")
        execute(terminal)
        wait_state(terminal, "Error")
        visible(terminal, "presupuesto de 256 MiB")
        terminal.send(ESC)
        move_to(terminal, "Ancho")
        focused(terminal, "8000")
        terminal.send(ESC)
        terminal.send("\x1b[B\x1b[B\r")
        expect_identity_heading(terminal, "RESULTADOS DE LA SESIÓN")
        terminal.send("\x1b[F")
        visible(terminal, "Resultado conservado · ejecución #2", "Equivalencia: SI")
        terminal.send(ESC)
        terminal.send("\x1b[A\x1b[A\r")
        field(terminal, "Ancho", "7")
        field(terminal, "Alto", "5")
        execute(terminal)
        wait_state(terminal, "Finalizado")
        visible(terminal, "Ejecución #4", "Equivalencia: SI")
        assert terminal.identity() == identity
        assert b"\x1b[2J" not in terminal.raw
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send("q")
        assert terminal.process.wait(timeout=5) == 0
        print(f"PASS: comparación/patrón, escalabilidad/generado, errores, recuperación, IDs y foco {columns}x{rows}")
    finally:
        terminal.close()


def check_active_close(binary, cwd):
    import os
    gate = str(Path(cwd) / "worker-gate")
    os.mkfifo(gate)
    os.environ["CONWAY_TEST_GATE"] = gate
    terminal = Terminal(binary, cwd, 120, 40)
    try:
        terminal.send("\r\r")
        field(terminal, "Ancho", "7")
        field(terminal, "Alto", "5")
        field(terminal, "Generaciones", "3")
        field(terminal, "Calentamientos", "0")
        field(terminal, "Repeticiones", "1")
        field(terminal, "Hilos OpenMP", "2")
        execute(terminal)
        wait_state(terminal, "Preparando entrada")
        terminal.send(ESC)
        focused(terminal, "Ejecutar experimento")
        terminal.send("\r" * 8)
        visible(terminal, "Ya hay un experimento activo")
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send("2")
        visible(terminal, "Simulación disponible al finalizar")
        terminal.send("\x1b[B" * 5 + "\r")
        visible(terminal, "Modo didáctico disponible al finalizar")
        terminal.send("C")
        visible(terminal, "Catálogo disponible al finalizar")
        # Navegar a Resultados y completar sin que el worker cambie la página activa.
        terminal.send("1\x1b[B\x1b[B\r")
        expect_identity_heading(terminal, "RESULTADOS DE LA SESIÓN")
        with open(gate, "w") as stream:
            stream.write("1")
        wait_state(terminal, "Finalizado")
        expect_identity_heading(terminal, "RESULTADOS DE LA SESIÓN")
        visible(terminal, "Ejecución #1", "Equivalencia: SI")
        terminal.send(ESC)
        terminal.send("\x1b[A\x1b[A\r")
        execute(terminal)
        wait_state(terminal, "Preparando entrada")
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send("q")
        visible(terminal, "Salida pendiente")
        assert terminal.process.poll() is None
        with open(gate, "w") as stream:
            stream.write("2")
        import time
        deadline = time.monotonic() + 15
        while terminal.process.poll() is None:
            assert time.monotonic() < deadline
            terminal.read()
        assert terminal.process.returncode == 0
        assert b"\x1b[2J" not in terminal.raw
        print("PASS: duplicados, navegación activa, exclusión de cálculos, publicación y cierre diferido con compuerta")
    finally:
        # Desbloquear el worker también si falla una aserción de la prueba.
        try:
            fd = os.open(gate, os.O_WRONLY | os.O_NONBLOCK)
            os.write(fd, b"x")
            os.close(fd)
        except OSError:
            pass
        terminal.close()
        os.environ.pop("CONWAY_TEST_GATE", None)


if __name__ == "__main__":
    import os
    os.environ["OMP_THREAD_LIMIT"] = "2"
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="conway-forms-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "cedar.txt").write_text("3 7\n" + "0000000\n" * 3)
        for size in [(120, 40), (80, 24)]:
            check(binary, cwd, *size)
        check_active_close(str(Path(sys.argv[2]).resolve()), cwd)

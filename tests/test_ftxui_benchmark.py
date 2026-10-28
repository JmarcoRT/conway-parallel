"""Formularios experimentales reales en PTY: edición, validación y foco."""
from pathlib import Path
import sys
import tempfile
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal as BaseTerminal, ROW
from test_ftxui_menu import expect_focus, expect_identity_heading, pixels, CYAN, CHARCOAL

class Terminal(BaseTerminal):
    def read(self):
        # Hundreds of UI actions used to each wait 350 ms even after rendering
        # finished. Drain the complete burst; keep 350 ms for silent observations.
        super().read(idle_timeout=0.05)


TAB = "\t"
BACK_TAB = "\x1b[Z"
ESC = "\x1b"


def visible(terminal, *values):
    terminal.assert_fits()
    text = " ".join(line.strip("│ ") for line in terminal.screen().values())
    for value in values:
        assert value in text, (value, text)


def scroll_visible(terminal, *values):
    terminal.send("\x1b[H")
    for _ in range(90):
        text = " ".join(line.strip("│ ") for line in terminal.screen().values())
        if all(value in text for value in values):
            visible(terminal, *values)
            return
        terminal.send("\x1b[B")
    raise AssertionError((values, terminal.screen()))


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
    for _ in range(20):
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
    terminal.send("\r")


def check(binary, cwd, columns, rows):
    terminal = Terminal(binary, cwd, columns, rows)
    try:
        identity = terminal.identity()
        terminal.send("3o4c2\rC\ru1\r")
        expect_identity_heading(terminal, "COMPARACIÓN DE MOTORES")
        visible(terminal, "cedar.txt", "7 × 3")
        execute(terminal)
        visible(terminal, "Selecciona al menos dos participantes")
        move_to(terminal, "OpenMP"); terminal.send(" ")
        field(terminal, "Generaciones", "0")
        execute(terminal); focused(terminal, "Generaciones"); visible(terminal, "Entero entre 1")
        edit(terminal, "3")
        field(terminal, "Calentamientos", "0"); field(terminal, "Repeticiones", "2")
        field(terminal, "Hilos OpenMP", "2"); execute(terminal); wait_state(terminal, "Finalizado")
        scroll_visible(terminal, "IDENTIFICACIÓN", "Entrada: cedar.txt")
        scroll_visible(terminal, "Speedup:", "Equivalencia: SI")
        terminal.send("\x1b[C")
        scroll_visible(terminal, "ESTADÍSTICAS DESCRIPTIVAS")
        scroll_visible(terminal, "Desviación estándar poblacional")
        terminal.send("\x1b[F"); visible(terminal, "Ronda 1")
        terminal.send("\x1b[C"); scroll_visible(terminal, "TIEMPO PRINCIPAL")
        scroll_visible(terminal, "CÓMO COMPARAMOS"); scroll_visible(terminal, "Speedup =")
        terminal.send("\x1b[C"); before=terminal.raw
        terminal.send("\r" * 12); assert terminal.raw==before, "Enter duplicated finished experiment"
        terminal.send(ESC); terminal.send(ESC); terminal.send("\x1b[B\r")
        expect_identity_heading(terminal, "ESCALABILIDAD")
        move_to(terminal,"Patrón cargado");terminal.send("\x1b[F\r")
        check_sources(terminal, ["7", "5", "30", "42"], change=True)
        field(terminal,"Generaciones","3");field(terminal,"Calentamientos","0");field(terminal,"Repeticiones","2")
        field(terminal,"Lista de hilos","1,1");execute(terminal)
        focused(terminal,"Lista de hilos");visible(terminal,"recursos duplicados")
        edit(terminal,"4,1,2");execute(terminal);wait_state(terminal,"Finalizado")
        visible(terminal,"Ejecución #2")
        scroll_visible(terminal,"Entrada: Tablero generado","7 × 5")
        scroll_visible(terminal,"TABLA DE ESCALABILIDAD");scroll_visible(terminal,"Eje Y: speedup")
        terminal.send("\x1b[C");scroll_visible(terminal,"ESTADÍSTICAS","Sec. compartida")
        terminal.send("\x1b[F");visible(terminal,"Ronda 1","Contexto conservado:")
        terminal.send(ESC);terminal.send(ESC);terminal.send("\x1b[A\r")
        move_to(terminal,"Patrón cargado");terminal.send("\x1b[F\r")
        field(terminal,"Ancho","8000");field(terminal,"Alto","8000");execute(terminal)
        visible(terminal,"256 MiB")
        terminal.send(ESC);terminal.send("\x1b[B\x1b[B\r")
        expect_identity_heading(terminal,"RESULTADOS DE LA SESIÓN")
        scroll_visible(terminal,"Ejecución #2");terminal.send("\x1b[D");scroll_visible(terminal,"Equivalencia: SI")
        terminal.send(ESC);terminal.send("\x1b[A\x1b[A\r")
        field(terminal,"Ancho","7");field(terminal,"Alto","5");execute(terminal);wait_state(terminal,"Finalizado")
        visible(terminal,"Ejecución #3")
        scroll_visible(terminal,"Equivalencia: SI")
        assert terminal.identity()==identity
        terminal.send(ESC);terminal.send(ESC);terminal.send("\x1b[B\x1b[B\r")
        scroll_visible(terminal,"Ejecución #3")
        before=terminal.raw;terminal.read();assert before==terminal.raw
        terminal.send("\x1b[C");scroll_visible(terminal,"ESTADÍSTICAS DESCRIPTIVAS")
        terminal.send(ESC);terminal.send("\r");scroll_visible(terminal,"ESTADÍSTICAS DESCRIPTIVAS")
        assert b"\x1b[2J" not in terminal.raw
        terminal.send(ESC);terminal.send(ESC);terminal.send("q");assert terminal.process.wait(timeout=5)==0
        print(f"PASS local comparison/scaling, form errors, session preservation and focus {columns}x{rows}", flush=True)
    finally:
        terminal.close()


def check_active_close(binary, cwd):
    import os
    gate = str(Path(cwd) / "worker-gate")
    os.mkfifo(gate)
    os.environ["CONWAY_TEST_GATE"] = gate
    terminal = Terminal(binary, cwd, 120, 40)
    try:
        terminal.send("b\r")
        field(terminal, "Ancho", "7")
        field(terminal, "Alto", "5")
        field(terminal, "Generaciones", "3")
        field(terminal, "Calentamientos", "0")
        field(terminal, "Repeticiones", "1")
        move_to(terminal, "OpenMP"); terminal.send(" ")
        field(terminal, "Hilos OpenMP", "2")
        execute(terminal)
        wait_state(terminal, "Preparando entrada")
        terminal.send(ESC)
        focused(terminal, "Ejecutar experimento")
        terminal.send("\r" * 8)
        visible(terminal, "Ya hay un experimento activo")
        terminal.send(ESC)
        terminal.send(ESC)
        terminal.send("d")
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
        scroll_visible(terminal, "Equivalencia: SI")
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

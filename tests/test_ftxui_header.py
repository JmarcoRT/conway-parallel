"""Comprueba el encabezado visible tras eventos reales en una PTY."""
import fcntl
import os
from pathlib import Path
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
ROW = re.compile(r"\x1b\[(\d+);1H(.*?)(?=\x1b\[\d+;1H|\Z)", re.S)


# Instantanea tipografica: tres filas, mismo ancho y dos palabras reconocibles.
EXPANDED_TITLE = (
    "╔═╗ ╔═╗ ╔╗╔ ╦ ╦ ╔═╗ ╦ ╦   ╔═╗ ╔═╗ ╦═╗ ╔═╗ ╦   ╦   ╔═╗ ╦  ",
    "║   ║ ║ ║║║ ║║║ ╠═╣ ╚╦╝   ╠═╝ ╠═╣ ╠╦╝ ╠═╣ ║   ║   ║╣  ║  ",
    "╚═╝ ╚═╝ ╝╚╝ ╚╩╝ ╩ ╩  ╩    ╩   ╩ ╩ ╩╚═ ╩ ╩ ╩═╝ ╩═╝ ╚═╝ ╩═╝",
)


class Terminal:
    def __init__(self, binary, cwd, columns, rows):
        self.columns = columns
        self.rows = rows
        self.master, slave = os.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
        env.pop("NO_COLOR", None)
        self.process = subprocess.Popen([binary], cwd=cwd, env=env,
                                        stdin=slave, stdout=slave, stderr=slave)
        os.close(slave)
        self.raw = b""
        self.read()

    def read(self):
        deadline = time.monotonic() + 0.35
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], 0.03)[0]:
                try:
                    self.raw += os.read(self.master, 65536)
                except OSError:
                    break

    def send(self, key):
        os.write(self.master, key.encode())
        self.read()

    def screen(self):
        lines = {}
        for match in ROW.finditer(self.raw.decode("utf-8", errors="replace")):
            lines[int(match[1])] = ANSI.sub("", match[2]).replace("\r", "").replace("\n", "")
        return lines

    def header(self):
        lines = self.screen()
        end = next((row for row in sorted(lines) if row > 1 and lines[row].startswith(("└", "╰"))), None)
        assert end is not None, lines
        return "\n".join(lines[row] for row in sorted(lines) if row < end)

    def assert_fits(self):
        lines = self.screen()
        assert max(lines) <= self.rows, lines
        assert all(len(line) == self.columns for line in lines.values()), lines

    def expect(self, *values):
        self.assert_fits()
        text = self.header()
        if self.columns >= 64 and 18 <= self.rows <= 28:
            values = [value.replace("OpenMP solicitado:", "OMP sol.:") for value in values]
        if 44 <= self.columns < 64:
            values = [value.replace("Patrón: ", "").replace("Motor: ", "")
                      .replace("OpenMP solicitado:", "OMP sol.:") for value in values]
        for value in values:
            assert value in text, (value, text)
        assert "efectivos" not in text.lower(), text

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=3)
        os.close(self.master)


def check(binary, cwd, columns, rows):
    terminal = Terminal(binary, cwd, columns, rows)
    try:
        terminal.expect("Sin patrón", "Motor: Secuencial", "OpenMP solicitado:")
        expanded = columns >= 120 and rows >= 40
        title_rows = EXPANDED_TITLE if expanded else ("CONWAY PARALLEL",)
        header_lines = terminal.header().splitlines()
        for index, title in enumerate(title_rows, start=1):
            line = header_lines[index]
            assert title in line, (title, line)
            assert abs(line.index(title) - (columns - len(title)) // 2) <= 1
            title_start = terminal.raw.index(title.encode())
            title_style = terminal.raw[max(0, title_start - 50):title_start]
            assert b"38;2;83;191;208m" in title_style, title_style
            assert b"\x1b[1m" in title_style, title_style
        # Los encabezados de 80x24 y 50x24 conservan dos filas interiores.
        if (columns, rows) in ((80, 24), (50, 24)):
            assert len(header_lines) == 3, header_lines
        before = terminal.raw
        terminal.read()
        assert terminal.raw == before, "La pantalla pausada genero refrescos"
        before = len(terminal.raw)
        terminal.send("\x1b[B")
        delta = terminal.raw[before:]
        assert all(title.encode() not in delta for title in title_rows), delta
        assert b"\x1b[2J" not in delta, delta
        terminal.send("\x1b[A")
        if columns >= 64 and rows > 28:
            terminal.expect("Simulación y análisis de programación paralela")
        terminal.send("C")
        terminal.send("\x1b[B")  # Navegar no carga el patron previsualizado.
        terminal.expect("Sin patrón")
        terminal.send("\x1b[A")
        terminal.send("\r")
        terminal.expect("Patrón: cedar.txt", "3 × 7")
        terminal.send("3")
        terminal.expect("Patrón: cedar.txt", "Motor: Secuencial")
        terminal.send("\x1b[B")
        terminal.send("\r")
        terminal.expect("Motor: OpenMP", "Patrón: cedar.txt")
        terminal.send("4")
        terminal.send("\x1b[B")
        terminal.send("\r")
        terminal.expect("OpenMP solicitado: 2", "Motor: OpenMP")
        terminal.send("4")
        terminal.expect("OpenMP solicitado: 2", "3 × 7")
        terminal.send("\x1b")
        terminal.send("C")
        terminal.send("\x1b[B")
        terminal.send("\r")
        terminal.expect("Patrón: willow.txt", "6 × 4", "OpenMP solicitado: 2")
        terminal.send("2")
        terminal.send("n")
        terminal.send("r")
        terminal.expect("Patrón: willow.txt", "6 × 4", "Motor: OpenMP", "OpenMP solicitado: 2")
        terminal.send("\x1b")
        terminal.expect("Patrón: willow.txt", "6 × 4", "OpenMP solicitado: 2")
        terminal.send("q")
        assert terminal.process.wait(timeout=3) == 0
        print(f"PASS: encabezado dinamico y navegacion {columns}x{rows}")
    finally:
        terminal.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="conway-header-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "cedar.txt").write_text("3 7\n" + "0000000\n" * 3)
        (patterns / "willow.txt").write_text("6 4\n" + "0000\n" * 6)
        for size in [(160, 50), (120, 40), (80, 24), (50, 24), (80, 16), (32, 24)]:
            check(binary, cwd, *size)

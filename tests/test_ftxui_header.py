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

    def expect(self, *values):
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
        terminal.expect("CONWAY PARALLEL", "Sin patrón", "Motor: Secuencial", "OpenMP solicitado:")
        title_start = terminal.raw.index(b"CONWAY PARALLEL")
        title_style = terminal.raw[max(0, title_start - 50):title_start]
        assert b"38;2;83;191;208m" in title_style, title_style
        assert b"\x1b[1m" in title_style, title_style
        title = next(line for line in terminal.header().splitlines() if "CONWAY PARALLEL" in line)
        assert abs(title.index("CONWAY PARALLEL") - (columns - len("CONWAY PARALLEL")) // 2) <= 1
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
        for size in [(120, 40), (50, 24), (80, 16), (32, 24)]:
            check(binary, cwd, *size)

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
    "███ ███ █  █ █   █ ███ █ █   ███ ███ ███ ███ █   █   ███ █  ",
    "█   █ █ ██ █ █ █ █ █▄█  █    ██▀ █▄█ ██▄ █▄█ █   █   ██  █  ",
    "███ ███ █ ██ ██ ██ █ █  █    █   █ █ █ █ █ █ ███ ███ ███ ███",
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

    def identity_end(self):
        lines = self.screen()
        end = next((row for row in sorted(lines) if row > 1 and lines[row].startswith(("└", "╰"))), None)
        assert end is not None, lines
        return end

    def identity(self):
        lines = self.screen()
        return "\n".join(lines[row] for row in sorted(lines) if row <= self.identity_end())

    def context(self):
        return self.screen()[self.identity_end() + 1]

    def header(self):
        return self.identity() + "\n" + self.context()

    def assert_fits(self):
        lines = self.screen()
        assert max(lines) <= self.rows, lines
        assert all(len(line) == self.columns for line in lines.values()), lines

    def expect(self, *values):
        self.assert_fits()
        text = self.context()
        assert not any(label in self.identity() for label in ("Patrón:", "Motor:", "Hilos"))
        for value in values:
            value = value.replace("Sin patrón", "Sin cargar")
            value = re.sub(r"(\d+) × (\d+)", r"\1×\2", value)
            if self.columns < 64:
                value = value.replace("Patrón: ", "").replace("Motor: ", "")
                value = value.replace("OpenMP solicitado:", "Hilos:")
                if self.columns < 44:
                    value = value.replace("Secuencial", "Sec.").replace("OpenMP", "OMP")
                    value = value.replace("Hilos: ", "H:")
            else:
                value = value.replace("OpenMP solicitado:", "Hilos solicitados:")
            assert value in text, (value, text)
        assert "efectivos" not in text.lower(), text
        if "Secuencial" in text or "Sec." in text:
            assert not any(label in text for label in ("Hilos", "H:", "OMP")), text

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=3)
        os.close(self.master)


def check(binary,cwd,columns,rows):
    terminal=Terminal(binary,cwd,columns,rows)
    try:
        if columns<40 or rows<18:
            assert 'Terminal insuficiente' in '\n'.join(terminal.screen().values());terminal.send('q');terminal.process.wait(timeout=5);return
        terminal.expect('Sin patrón','Motor: Secuencial')
        identity=terminal.identity();before=len(terminal.raw);terminal.send('\t')
        assert b'CONWAY PARALLEL' not in terminal.raw[before:]
        terminal.send('c');terminal.send('\r')
        terminal.expect('Sin patrón');terminal.send('u');terminal.expect('Patrón: cedar.txt','3 × 7')
        assert identity==terminal.identity()
        terminal.send('m');terminal.send('m');terminal.expect('Motor: MPI')
        terminal.send('r');terminal.send('\x1b[H'+'\x1b[3~'*6+'2\r');terminal.expect('P:2')
        terminal.send('q');assert terminal.process.wait(timeout=5)==0
        print(f'PASS header identity and confirmed/candidate separation {columns}x{rows}',flush=True)
    finally:terminal.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="conway-header-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "cedar.txt").write_text("3 7\n" + "0000000\n" * 3)
        (patterns / "willow.txt").write_text("6 4\n" + "0000\n" * 6)
        (patterns / ("zzz_" + "nombre_largo_" * 15 + ".txt")).write_text("7 11\n" + "00000000000\n" * 7)
        for size in [(160, 50), (120, 40), (80, 24), (50, 24), (80, 16), (32, 24)]:
            check(binary, cwd, *size)

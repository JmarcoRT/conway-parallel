"""State waits must reject a hung/dead app even when PTY reads are faster."""
import sys
from unittest.mock import patch
sys.dont_write_bytecode = True
from test_ftxui_benchmark import wait_state


class Terminal:
    def __init__(self, finish=False, dead=False):
        self.now = 0
        self.reads = 0
        self.finish = finish
        self.process = self
        self.dead = dead

    def poll(self):
        return 1 if self.dead else None

    def screen(self):
        return {1: 'Comparación completada' if self.finish and self.reads >= 2 else 'Ejecutando benchmark'}

    def read(self):
        self.reads += 1
        self.now += 0.03

    def assert_fits(self):
        pass


def run(terminal):
    with patch('time.monotonic', side_effect=lambda: terminal.now):
        wait_state(terminal, 'Finalizado', timeout=0.1)


run(Terminal(finish=True))
for terminal in (Terminal(), Terminal(dead=True)):
    try:
        run(terminal)
    except AssertionError:
        continue
    raise AssertionError('PTY wait accepted a hung or terminated app')
print('PASS bounded state wait rejects hangs and dead apps, accepts real completion')

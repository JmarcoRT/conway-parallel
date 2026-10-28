"""Controls and process lifecycle in the real FTXUI application on a PTY."""
from pathlib import Path
import os
import signal
import sys
import time
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal


def workers(parent):
    records = {}
    for item in Path('/proc').iterdir():
        if not item.name.isdecimal():
            continue
        try:
            fields = (item / 'stat').read_text().rsplit(')', 1)[1].split()
            records[int(item.name)] = (int(fields[1]), (item / 'comm').read_text().strip())
        except (OSError, ValueError):
            pass
    result = []
    for pid, (_, name) in records.items():
        ancestor = pid
        for _ in range(32):
            if ancestor == parent:
                if name.startswith('conway_mpi_work'):
                    result.append(pid)
                break
            ancestor = records.get(ancestor, (0, ''))[0]
            if not ancestor:
                break
    return result


def wait(terminal, predicate, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        terminal.read()
        if predicate():
            return
    raise AssertionError(terminal.screen())


def text(terminal):
    return '\n'.join(terminal.screen().values())


def generation(terminal):
    import re
    matches = re.findall(r'Generación (\d+)', text(terminal))
    return int(matches[-1]) if matches else None


def check(binary, root):
    terminal = Terminal(binary, root, 120, 40)
    observed = set()
    try:
        terminal.send('3'); terminal.send('m')
        assert not workers(terminal.process.pid), 'selection launched MPI before session'
        terminal.send('C'); terminal.send('m')
        terminal.send(str(Path(root) / 'patterns/blinker.txt') + '\r')
        terminal.send('2')
        wait(terminal, lambda: len(workers(terminal.process.pid)) == 1 and 'Pausado' in text(terminal))
        observed.update(workers(terminal.process.pid))
        assert generation(terminal) == 0
        terminal.send('n'); wait(terminal, lambda: generation(terminal) == 1 and 'Pausado' in text(terminal))
        terminal.send('\x1b'); terminal.send('2')
        assert generation(terminal) == 1, 'Esc/menu/resume reset session'
        terminal.send(' '); time.sleep(.3); terminal.send(' ')
        wait(terminal, lambda: 'Pausado' in text(terminal))
        paused = generation(terminal)
        time.sleep(.4); terminal.read(); assert generation(terminal) == paused
        terminal.send('\x1b'); terminal.send('3'); terminal.send('c')
        terminal.send('\x1b[H' + '\x1b[3~' * 10 + '0\r')
        assert 'P debe ser un entero' in text(terminal), 'invalid P was accepted'
        terminal.send('\x1b[H' + '\x1b[3~' * 10 + '2\r')
        terminal.send('2')
        wait(terminal, lambda: len(workers(terminal.process.pid)) == 2 and 'Pausado' in text(terminal))
        observed.update(workers(terminal.process.pid))
        assert generation(terminal) == paused, 'P change lost generation'
        terminal.send('\x1b'); terminal.send('3'); terminal.send('o')
        wait(terminal, lambda: not workers(terminal.process.pid))
        terminal.send('2'); assert generation(terminal) == paused
        terminal.send('n'); assert generation(terminal) == paused + 1
        terminal.send('\x1b'); terminal.send('3'); terminal.send('m'); terminal.send('2')
        wait(terminal, lambda: len(workers(terminal.process.pid)) == 2 and 'Pausado' in text(terminal))
        observed.update(workers(terminal.process.pid))
        assert generation(terminal) == paused + 1
        terminal.send('r'); wait(terminal, lambda: generation(terminal) == 0 and 'Pausado' in text(terminal))
        # Actual worker failure, UI must remain responsive and preserve current.
        os.kill(workers(terminal.process.pid)[0], signal.SIGKILL)
        terminal.send('n')
        wait(terminal, lambda: 'No se pudo completar' in text(terminal))
        assert generation(terminal) == 0
        terminal.send('u')
        wait(terminal, lambda: len(workers(terminal.process.pid)) == 2 and 'Pausado' in text(terminal))
        observed.update(workers(terminal.process.pid))
        terminal.send('q'); wait(terminal, lambda: not workers(terminal.process.pid) and 'MENÚ PRINCIPAL' in text(terminal))
        terminal.send('2')
        wait(terminal, lambda: len(workers(terminal.process.pid)) == 2 and 'Pausado' in text(terminal))
        observed.update(workers(terminal.process.pid))
        assert generation(terminal) == 0, 'new session did not start from initial pattern'
        terminal.send('\x1b'); terminal.send('0')
        assert terminal.process.wait(timeout=5) == 0
        assert not any(Path(f'/proc/{pid}').exists() for pid in observed), 'residual MPI worker'
        print('PASS PTY MPI selector/P, confirmed generations, pause/resume, reset, transfers, fault/recovery, close')
    finally:
        if terminal.process.poll() is None:
            terminal.send('\x1b'); terminal.send('0')
        terminal.close()


if __name__ == '__main__':
    check(sys.argv[1], sys.argv[2])

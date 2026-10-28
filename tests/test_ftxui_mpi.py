"""Shared PTY/MPI process helpers; lifecycle coverage lives in mpi_simulation and didactic tests."""
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

"""Inspect one real hybrid capture through FTXUI, including monochrome labels."""
import os
from pathlib import Path
import re
import shlex
import sys
import tempfile

sys.dont_write_bytecode = True
from test_ftxui_benchmark import Terminal
from test_ftxui_mpi import text, wait, workers
from test_ftxui_responsive import resize


def edit(t, value):
    t.send('\x1b[H' + '\x1b[3~' * 8 + value)


def load(t, path):
    for key in ('C', 'm', str(path) + '\r', 'u'):
        t.send(key)


def enter(t, validate=False, threads=3):
    t.send('d')
    wait(t, lambda: 'MODO DIDÁCTICO' in text(t))
    t.send('m')
    assert 'MOTOR DIDÁCTICO' in text(t) and 'MPI + OpenMP' in text(t), text(t)
    t.send('\x1b[H' + '\x1b[B' * 3 + '\r')
    assert 'Hilos por rank' in text(t), text(t)
    if validate:
        edit(t, '129')
        t.send('\r')
        assert 'Procesos MPI entre 1 y 128' in text(t), text(t)
    edit(t, '2')
    t.send('\t')
    edit(t, str(threads))
    t.send('\r')
    wait(t, lambda: 'Paso 1 de 6' in text(t) and 'En pausa' in text(t) and 'R0 g0' in text(t))


def find_line(t, pattern):
    t.send('\x1b[H')
    for _ in range(30):
        screen = text(t)
        for line in screen.splitlines():
            if re.search(pattern, line):
                t.assert_fits()
                return line.strip()
        t.send('\x1b[6~')
    raise AssertionError((pattern, text(t)))


def check(binary, root, directory):
    wrapper = directory / 'monochrome'
    wrapper.write_text('#!/bin/sh\nexport NO_COLOR=1\nexec ' + shlex.quote(binary) + '\n')
    wrapper.chmod(0o700)
    t = Terminal(str(wrapper), root, 50, 24)
    try:
        t.send('mo')
        t.send('r1')
        load(t, root / 'patterns/blinker.txt')
        enter(t, validate=True)
        t.send('n')
        t.send('v')
        print('Halo inferior en R0:', find_line(t, r'H de R1 g3|H de R1.*g3'), flush=True)
        # The halo row contains the live central cell.
        find_line(t, r'╎\s*\.\s*[╎┆]\s*\.\s*[╎┆]\s*O|┆\s*\.\s*┆\s*\.\s*┆\s*O')
        t.send(']')
        t.send('n')
        print('Halo superior en R1:', find_line(t, r'H de R0 g2|H de R0.*g2'), flush=True)
        find_line(t, r'┆\s*\.\s*┆\s*\.\s*┆\s*O')
        t.send('n')
        t.send('v')
        t.send('\x1b[A')
        print('Global:', find_line(t, r'> R0 g2'), flush=True)
        t.send('\r')
        wait(t, lambda: 'R0/H1' in text(t) and 'Paso 4 de 6' in text(t))
        print('Local:', find_line(t, r'> H1 g2'), flush=True)
        find_line(t, r'3 solicitados · 2 efectivos')
        t.send(']')
        assert 'R1/H0' in text(t) and 'Paso 4 de 6' in text(t), text(t)
        t.send('d')
        assert 'AYUDA MPI' in text(t), text(t)
        t.send('d')
        assert 'R1/H0' in text(t), text(t)
        resize(t, 40, 18)
        t.read()
        assert 'R1/H0' in text(t) and 'Paso 4 de 6' in text(t), text(t)
        t.assert_fits()
        t.send('nnn')
        wait(t, lambda: 'Generación 1' in text(t) and 'Paso 1 de 6' in text(t))
        t.send('r')
        wait(t, lambda: 'Generación 0' in text(t) and 'Paso 1 de 6' in text(t) and 'En pausa' in text(t))
        assert b'48;2;98;232;137m' not in t.raw
        t.send('\x1b')
        wait(t, lambda: not workers(t.process.pid))
        assert 'OMP · H:1' in text(t), text(t)

        single = directory / 'one-row.txt'
        single.write_text('1 5\n00100\n')
        resize(t, 50, 24)
        t.read()
        load(t, single)
        enter(t)
        t.send('nnnv]')
        assert 'R1' in text(t) and 'Paso 4 de 6' in text(t), text(t)
        find_line(t, r'Sin filas propias')
        t.send('[')
        assert 'R0/H0' in text(t), text(t)
        find_line(t, r'1 con trabajo')
        t.send('\x1b')
        t.send('0')
        assert t.process.wait(timeout=8) == 0
    finally:
        t.close()


def check_color(binary, root):
    t = Terminal(binary, root, 120, 32)
    try:
        load(t, root / 'patterns/blinker.txt')
        enter(t, threads=2)
        assert 'Tablero completo' in text(t) and 'Generación actual' in text(t), text(t)
        assert b'38;2;232;158;90m' in t.raw and b'38;2;90;143;232m' in t.raw
        assert b'48;2;98;232;137m' in t.raw
        t.send('nnn')
        assert '2 solicitados · 2 efectivos' in text(t), text(t)
        t.assert_fits()
        t.send('\x1b')
        wait(t, lambda: not workers(t.process.pid))
    finally:
        t.close()


if __name__ == '__main__':
    os.environ.update(OMP_NUM_THREADS='2', OMP_THREAD_LIMIT='2')
    binary, root = str(Path(sys.argv[1]).resolve()), Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix='conway-hybrid-pty-') as temporary:
        check(binary, root, Path(temporary))
    check_color(binary, root)

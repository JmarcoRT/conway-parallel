"""Grouped menu and immutable catalog confirmation, real playback and drainage."""
from pathlib import Path
import os
import re
import shlex
import sys
import tempfile
import time
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal, ROW
from test_ftxui_mpi import wait,text,workers
from test_ftxui_responsive import resize

def load(t,path,confirm=False):
    t.send('c');t.send('m');t.send(str(path)+'\r')
    wait(t,lambda:'generación 0' in text(t))
    if confirm:t.send('u')

def generation(t):
    m=re.findall(r'CURRENT · generación (\d+)',text(t));return int(m[-1]) if m else None

def raster_rows(terminal):
    """Decode actual compact 5x5 fills, including alternating checker shades.

    Decode two-column cells or paired half-block rows. Read physical rows from
    the PTY so playback assertions inspect pixels rather than generation labels.
    """
    lines = {}
    for match in ROW.finditer(terminal.raw.decode('utf-8', errors='replace')):
        background = foreground = None
        pixels = []
        lower = []
        def state(color):
            if color and len(color) == 3:
                red, green, blue = color
                if green > 100 and green > red * 1.3 and green > blue * 1.3:
                    return 'O'
                if 20 <= red <= 60 and 40 <= green <= 80 and 30 <= blue <= 90:
                    return '.'
            return ' '
        for token in re.findall(r'\x1b\[[0-?]*[ -/]*[@-~]|[^\x1b]', match[2]):
            if token.startswith('\x1b'):
                if not token.endswith('m'):
                    continue
                codes = [int(value or 0) for value in token[2:-1].split(';')]
                i = 0
                while i < len(codes):
                    code = codes[i]
                    if code == 0:
                        background = foreground = None
                    elif code == 49:
                        background = None
                    elif code == 39:
                        foreground = None
                    elif code in (38, 48) and i + 1 < len(codes):
                        count = 4 if codes[i + 1] == 2 else 2
                        if code == 48:
                            background = tuple(codes[i + 2:i + count + 1])
                        else:
                            foreground = tuple(codes[i + 2:i + count + 1])
                        i += count
                    i += 1
            elif token not in '\r\n':
                value = ' '
                if token in '▓░':
                    value = 'O' if token == '▓' else '.'
                elif token in (' ', '▀'):
                    value = state(foreground if token == '▀' else background)
                pixels.append(value)
                lower.append(state(background) if token == '▀' else ' ')
        lines[int(match[1])] = (''.join(pixels), ''.join(lower))
    rows = []
    for line, lower in lines.values():
        half = re.search(r'(?<![O.])[O.]{5}(?![O.])', lower)
        if half:
            rows.extend([line[half.start():half.end()], half[0]])
            continue
        for match in re.finditer(r'(?<![O.])[O.]{10}(?![O.])', line):
            cells = match[0]
            assert all(cells[i] == cells[i + 1] for i in range(0, 10, 2)), line
            rows.append(cells[::2])
    return rows[:5]

def check(binary,root,size,no_color=False):
    t=Terminal(binary,root,*size)
    try:
        for label in ['MODOS PRINCIPALES','EXPLORAR PATRONES','CONFIGURACIÓN','Visualización didáctica','Benchmark','Catálogo de patrones','Seleccionar motor','Configurar recursos','Salir']:
            assert label in text(t),text(t)
        for label in ['Visualización didáctica','Benchmark','Catálogo de patrones','Seleccionar motor','Configurar recursos','Salir']:
            assert '> '+label in text(t),text(t)
            t.send('\t')
        t.send('r');assert 'sin recursos configurables' in ' '.join(row.strip('│ ') for row in t.screen().values());t.send('\x1b')
        t.send('m');t.send('m');assert not workers(t.process.pid)
        load(t,Path(root)/'patterns/blinker.txt')
        assert 'Sin cargar' in t.context() and 'MPI' in t.context(),t.context()
        assert 'Secuencial' in text(t) and 'En pausa' in text(t),text(t)
        assert not workers(t.process.pid)
        # Raster must contain actual grid cells, not just a title.
        assert len(raster_rows(t)) == 5, text(t)
        t.send(' ');wait(t,lambda:generation(t) is not None and generation(t)>=2)
        t.send(' ');paused=generation(t);raw=t.raw;t.read();assert t.raw==raw
        t.send('r');wait(t,lambda:generation(t)==0);assert 'En pausa' in text(t)
        t.send('\x1b[B'*2)
        pattern_rows=raster_rows(t)
        assert '..O..' in pattern_rows and '.OOO.' not in pattern_rows,text(t)
        t.send('n');wait(t,lambda:generation(t)==1)
        assert '.OOO.' in raster_rows(t),text(t)
        t.send('u');wait(t,lambda:'blinker.txt' in t.context())
        assert '5×5' in t.context() and 'MPI' in t.context()
        # Invalid manual input cannot confirm the old candidate accidentally.
        load(t,Path(root)/'patterns/block.txt');t.send('m');t.send('/no/such/pattern.txt\r')
        assert 'Error al cargar' in text(t);assert 'blinker.txt' in t.context()
        t.send('\x1b');assert generation(t)==0
        t.send('x');assert 'blinker.txt' in t.context()
        # Reentering catalogue and changing the highlighted row starts at zero.
        t.send('c');t.send('\x1b[B');t.send('\r')
        assert generation(t)==0 and 'En pausa' in text(t)
        assert 'blinker.txt' in t.context()
        # Keep the resize fixture independent of catalog ordering and additions.
        t.send('m');t.send(str(Path(root)/'patterns/blinker.txt')+'\r')
        wait(t,lambda:generation(t)==0)
        for dimensions in [(160,45),(50,24),(40,18),(120,40)]:
            resize(t,*dimensions);t.read();t.assert_fits()
            assert 'Esp' in text(t) and 'U usar' in text(t),text(t)
            if dimensions[0]<100:assert 'CURRENT · generación 0' in text(t),text(t)
        t.send('x');t.send('d')
        wait(t,lambda:'DIDÁCTICA MPI' in text(t) and 'Paso 1 de 6' in text(t))
        # Initial input, not the previously played generation, is captured.
        assert 'Generación 0' in text(t);t.send(' ')
        t.send('\x1b');wait(t,lambda:not workers(t.process.pid))
        t.send('b');t.send('\r');assert 'COMPARACIÓN DE MOTORES' in text(t)
        t.send('\x1b');t.send('\x1b');t.send('q');assert t.process.wait(timeout=8)==0
        assert not workers(t.process.pid)
        print(f'PASS grouped menu, sequential preview, immutable/manual confirmation, playback/reset, resize, MPI didactic and close {size}',flush=True)
    finally:
        if t.process.poll() is None:
            t.send('\x1b');t.send('x');t.send('q')
            try:t.process.wait(timeout=8)
            except Exception:pass
        t.close()

def monochrome(binary,root):
    with tempfile.TemporaryDirectory(prefix='catalog-no-color-') as directory:
        wrapper=Path(directory)/'run';wrapper.write_text('#!/bin/sh\nexport NO_COLOR=1\ninitial=$(stty -g)\n'+shlex.quote(binary)+'\nstatus=$?\nfinal=$(stty -g)\n[ \"$initial\" = \"$final\" ] && echo TERMINAL_RESTORED\nexit $status\n');wrapper.chmod(0o700)
        t=Terminal(str(wrapper),root,80,24)
        try:
            load(t,Path(root)/'patterns/blinker.txt')
            t.send('\x1b[B'*2)
            assert '▓▓' in text(t) and '░░' in text(t),text(t)
            t.send('n');wait(t,lambda:generation(t)==1)
            assert '.OOO.' in raster_rows(t),text(t)
            assert b'48;2;98;232;137m' not in t.raw
            t.send('x');t.send('q');assert t.process.wait(timeout=5)==0
            t.read();assert b'TERMINAL_RESTORED' in t.raw
            assert b'\x1b[?1049h' in t.raw and b'\x1b[?1049l' in t.raw
            print('PASS no-color grid states and actual blinker evolution',flush=True)
        finally:t.close()

def manual_snapshot(binary,root):
    with tempfile.TemporaryDirectory(prefix='catalog-validated-') as directory:
        path=Path(directory)/'immutable.txt';path.write_text('5 5\n00000\n00100\n00100\n00100\n00000\n')
        t=Terminal(binary,root,80,24)
        try:
            load(t,path);path.write_text('1 1\n0\n');t.send('u')
            assert 'immutable.txt (5×5)' in t.context(),t.context()
            t.send('q');assert t.process.wait(timeout=5)==0
            print('PASS confirming manual candidate never rereads changed file',flush=True)
        finally:t.close()

if __name__=='__main__':
    binary,root=[str(Path(p).resolve()) for p in sys.argv[1:]]
    for size in [(120,40),(50,24),(40,18)]:check(binary,root,size)
    monochrome(binary,root)
    manual_snapshot(binary,root)

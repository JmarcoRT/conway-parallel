"""Grouped menu and immutable catalog confirmation, real playback and drainage."""
from pathlib import Path
import os
import re
import shlex
import sys
import tempfile
import time
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal
from test_ftxui_mpi import wait,text,workers
from test_ftxui_responsive import resize
from test_ftxui_didactic_mpi_views import visual

def load(t,path,confirm=False):
    t.send('c');t.send('m');t.send(str(path)+'\r')
    wait(t,lambda:'generación 0' in text(t))
    if confirm:t.send('u')

def generation(t):
    m=re.findall(r'CURRENT · generación (\d+)',text(t));return int(m[-1]) if m else None

def raster_rows(view):
    # Wide catalogue has another panel and a gap before the actual grid.
    return [''.join(re.findall(r'│([O.?]) +(?=│)','│'+line.rsplit('││',1)[1]))[:5]
            for line in view.splitlines() if '││' in line]

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
        assert '┌──' in text(t) and '└──' in text(t),text(t)
        t.send(' ');wait(t,lambda:generation(t) is not None and generation(t)>=2)
        t.send(' ');paused=generation(t);raw=t.raw;t.read();assert t.raw==raw
        t.send('r');wait(t,lambda:generation(t)==0);assert 'En pausa' in text(t)
        t.send('\x1b[B'*2)
        raster=visual(t)
        pattern_rows=raster_rows(raster)
        assert '..O..' in pattern_rows and '.OOO.' not in pattern_rows,raster
        t.send('n');wait(t,lambda:generation(t)==1)
        raster=visual(t)
        assert '.OOO.' in raster_rows(raster),raster
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
        for dimensions in [(160,45),(50,24),(40,18),(120,40)]:
            resize(t,*dimensions);t.read();t.assert_fits()
            assert 'Esp' in text(t) and 'U usar' in text(t),text(t)
            if dimensions[0]<100:assert 'CURRENT · generación 0' in text(t),text(t)
        t.send('x');t.send('d')
        wait(t,lambda:'DIDÁCTICA MPI' in text(t) and 'capturada 1' in text(t))
        # Initial input, not the previously played generation, is captured.
        assert 'Visible G0' in text(t);t.send(' ')
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
            assert '░░│▓▓│▓▓│▓▓│░░' in text(t),text(t)
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

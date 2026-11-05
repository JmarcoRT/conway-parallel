"""Benchmark CUDA through real forms, result tabs and retained session results."""
from pathlib import Path
import os
import subprocess
import sys
sys.dont_write_bytecode=True
from test_ftxui_benchmark import Terminal, move_to, focus_text, field, execute, wait_state, scroll_visible, visible

binary,probe,root=sys.argv[1:]
binary=str(Path(binary).resolve());root=str(Path(root).resolve())
check=subprocess.run([probe,'--availability'],capture_output=True,text=True)
if check.returncode in (2,77):
    available=False
else:
    assert check.returncode==0,(check.stdout,check.stderr)
    available=True

def select(t,label,checked=True):
    move_to(t,label)
    if ('[x] '+label in focus_text(t))!=checked:t.send(' ')

def small(t):
    for label,value in [('Generaciones','4'),('Calentamientos','1'),('Repeticiones','2')]:field(t,label,value)

for size in [(120,40),(50,24)]:
    t=Terminal(binary,root,*size)
    try:
        for key in ['c','m',str(Path(root)/'patterns/pulsar.txt')+'\r','u']:t.send(key)
        normal=available and size==(120,40)
        if normal:
            for key in ['m','g','v','n','n']:t.send(key)
            visible(t,'Generación 2');t.send('x')
        t.send('b');t.send('\r')
        select(t,'CUDA');small(t)
        field(t,'Bloque CUDA','7x7');execute(t)
        if not available:
            scroll_visible(t,'CUDA no disponible')
            t.send('\x1b');t.send('\x1b');t.send('q')
            assert t.process.wait(timeout=5)==0
            continue
        visible(t,'Selecciona un bloque')
        field(t,'Bloque CUDA','16x16');execute(t);wait_state(t,'Finalizado')
        scroll_visible(t,'Bloque 16×16','Correcto')
        scroll_visible(t,'— No aplica.')
        t.send('\x1b[C');scroll_visible(t,'ESTADÍSTICAS POR MOTOR');t.send('d')
        scroll_visible(t,'Intervalo del stream:');scroll_visible(t,'Operación completa:')
        t.send('\x1b[C');scroll_visible(t,'QUÉ TIEMPO SE COMPARA');t.send('d')
        scroll_visible(t,'CUDA: lote residente');scroll_visible(t,'eficiencia N/D')
        t.send('\x1b');t.send('\x1b');t.send('\x1b[B\r')
        move_to(t,'OpenMP');t.send('\x1b[F');small(t)
        field(t,'Bloques CUDA','8x8, 16x16, 32x8');execute(t);wait_state(t,'Finalizado')
        for label in ['CUDA · bloque 8x8','CUDA · bloque 16x16','CUDA · bloque 32x8']:scroll_visible(t,label)
        scroll_visible(t,'Configuraciones de bloque en una GPU')
        t.send('\x1b');t.send('\x1b');t.send('\x1b[B\r')
        visible(t,'RESULTADOS DE LA SESIÓN');scroll_visible(t,'CUDA · bloque 32x8')
        before=t.raw;t.read();assert before==t.raw,'finished result keeps repainting'
        t.send('\x1b');t.send('\x1b')
        if normal:
            t.send('v');visible(t,'Generación 2');t.send('n');visible(t,'Generación 3');t.send('x')
        t.send('q');assert t.process.wait(timeout=5)==0
        print('PASS CUDA comparison, block study, samples/method, session retention and terminal',size,flush=True)
    finally:t.close()

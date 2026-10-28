"""Real small experiments: participant controls, MPI scaling and partial failures."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_benchmark import field, execute, visible, scroll_visible, move_to, focus_text, wait_state
from test_ftxui_mpi import workers


def select(t, label, checked=True):
    for _ in range(24):
        current = focus_text(t)
        if '[x] ' + label in current or '[ ] ' + label in current:
            if ('[x] ' + label in current) != checked:
                t.send(' ')
            return
        t.send('\t')
    raise AssertionError((label, t.screen()))


def small(t):
    for label, value in [('Generaciones', '3'), ('Calentamientos', '1'), ('Repeticiones', '1')]:
        field(t, label, value)


def open_comparison(t):
    t.send('C'); t.send('m'); t.send(str(Path(root) / 'patterns/blinker.txt') + '\r');t.send('u')
    t.send('1'); t.send('\r')


def check(binary, size):
    t = Terminal(binary, root, *size)
    try:
        open_comparison(t)
        select(t, 'Secuencial', True)
        execute(t)
        visible(t, 'Selecciona al menos dos participantes')
        select(t, 'OpenMP'); select(t, 'MPI')
        small(t); field(t, 'Hilos OpenMP', '2'); field(t, 'Procesos MPI', '0')
        execute(t); visible(t, 'Entero entre 1')
        field(t, 'Procesos MPI', '1')
        execute(t); wait_state(t, 'Finalizado')
        scroll_visible(t, 'MPI · proceso solicitado: 1')
        scroll_visible(t, 'Equivalencia: SI', 'Validez: SI')
        assert not workers(t.process.pid), 'ranks survived experiment'
        t.send('\x1b[C')
        scroll_visible(t, 'Auxiliares MPI', 'Snapshot:')
        scroll_visible(t, 'Calentamientos por sesión: 1')
        scroll_visible(t, 'Arranque:', 'Cierre:')
        t.send('\x1b[C'); scroll_visible(t, 'MPI_Wtime')
        scroll_visible(t, 'Cierre confirmado')
        t.send('\x1b'); select(t, 'Secuencial', False)
        # The warning can be above the current viewport; submission/results must preserve N/D.
        execute(t); wait_state(t, 'Finalizado')
        scroll_visible(t, 'Sin participante Secuencial')
        scroll_visible(t, 'Speedup: N/D', 'Eficiencia: N/D')
        t.send('\x1b'); t.send('\x1b'); t.send('\x1b[B\r')
        move_to(t, 'OpenMP'); t.send('\x1b[F')
        small(t); field(t, 'Lista de procesos', '1,1')
        execute(t); visible(t, 'recursos duplicados')
        field(t, 'Lista de procesos', '1,2')
        execute(t); wait_state(t, 'Finalizado')
        scroll_visible(t, 'MPI · procesos solicitados: 2')
        scroll_visible(t, 'Procesos →')
        t.send('\x1b[C'); scroll_visible(t, 'Auxiliares MPI')
        t.send('\x1b'); t.send('\x1b'); t.send('\x1b[B\r')
        visible(t, 'RESULTADOS DE LA SESIÓN')
        scroll_visible(t, 'MPI · procesos solicitados: 2')
        assert not workers(t.process.pid)
        before=t.raw;t.read();assert t.raw==before, 'periodic redraw while finished'
        t.send('\x1b'); t.send('\x1b'); t.send('0');assert t.process.wait(timeout=5)==0
        print(f'PASS three motors, no baseline, MPI scaling, invalid resources, session results {size}', flush=True)
    finally:
        t.close()


def failure(binary):
    with tempfile.TemporaryDirectory(prefix='conway-missing-worker-') as directory:
        copy=Path(directory)/'conway';shutil.copy2(binary,copy)
        t=Terminal(str(copy),root,80,24)
        try:
            open_comparison(t);select(t,'MPI');small(t);field(t,'Procesos MPI','1');execute(t)
            wait_state(t,'Error')
            scroll_visible(t,'ESTUDIO INCOMPLETO');scroll_visible(t,'Diagnóstico:')
            t.send('\x1b[C');scroll_visible(t,'Secuencial');scroll_visible(t,'Diagnóstico de sesión:')
            assert not workers(t.process.pid)
            t.send('\x1b');t.send('\x1b');t.send('\x1b[B\x1b[B\r')
            scroll_visible(t,'ESTUDIO INCOMPLETO')
            t.send('\x1b');t.send('\x1b');t.send('0');assert t.process.wait(timeout=5)==0
            print('PASS actual worker launch failure: partial samples, diagnostics and retained session result', flush=True)
        finally:
            t.close()


if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve());root=str(Path(sys.argv[2]).resolve())
    os.environ.update(OMP_NUM_THREADS='2', OMP_THREAD_LIMIT='2', OMP_DYNAMIC='FALSE')
    for size in [(120,40),(80,24),(50,24)]:
        check(binary,size)
    failure(binary)

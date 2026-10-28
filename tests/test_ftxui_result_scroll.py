"""Check actual viewport movement, last sample and stable form geometry in a PTY."""
import os
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_benchmark import field, execute, wait_state, move_to, visible
from test_ftxui_experimental_mpi import select
from test_ftxui_responsive import resize

def text(t):
    return '\n'.join(t.screen().values())

def check(binary, root):
    t=Terminal(binary, root,120,40)
    try:
        t.send('C');t.send('m');t.send(str(Path(root)/'patterns/blinker.txt')+'\r');t.send('u')
        t.send('1');t.send('\x1b[B\r')
        move_to(t,'OpenMP')
        def divider():
            lines=t.screen()
            return next(line.index('│',1) for line in lines.values() if 'ENTRADA' in line)
        before=divider()
        t.send('\x1b[F');assert divider()==before,t.screen()
        t.send('\x1b[H');assert divider()==before,t.screen()
        resize(t,100,30);before=divider()
        t.send('\x1b[F');assert divider()==before,t.screen()
        resize(t,50,24);t.send('\x1b[H');t.send('\x1b[F')
        resize(t,120,40);before=divider();t.send('\x1b[H');assert divider()==before,t.screen()
        t.send('\x1b');t.send('\x1b[A\r')
        select(t,'OpenMP')
        for label,value in [('Generaciones','3'),('Calentamientos','0'),('Repeticiones','8'),('Hilos OpenMP','2')]:field(t,label,value)
        execute(t);wait_state(t,'Finalizado')
        assert 'Simulación:' in t.context() and 'Motor:' not in t.context(),t.context()
        t.send('\x1b[C')
        for size in [(120,40),(50,24),(80,24),(120,40)]:
            print(f'Checking viewport {size}',flush=True)
            resize(t,*size);t.send('\x1b[H');first=text(t)
            assert '#16 · Ronda' not in first,first
            for _ in range(22):t.send('\x1b[B')
            middle=text(t)
            assert middle!=first,(first,middle)
            assert 'Mediana:' not in middle,middle
            # The pinned tab and position survive scrolling and resize.
            assert '[Muestras]' in middle and 'Bloque ' in middle,middle
            for _ in range(100):
                if '#16 · Ronda' in text(t):break
                t.send('\x1b[B')
            else:raise AssertionError(t.screen())
            t.send('\x1b[F');last=text(t)
            assert '#16 · Ronda' in last and 'Validez de muestra: SI' in last,last
            assert '[Muestras]' in last and 'Esc' in last,last
            t.send('\x1b[A');assert text(t)!=last
        t.send('\x1b[C');t.send('\x1b[F');visible(t,'divisor N')
        print('PASS actual scroll to last of 16 samples, pinned tabs/help, stable columns, small terminal and resizing')
    finally:t.close()

if __name__=='__main__':
    os.environ.update(OMP_NUM_THREADS='2',OMP_THREAD_LIMIT='2',OMP_DYNAMIC='FALSE')
    check(str(Path(sys.argv[1]).resolve()),str(Path(sys.argv[2]).resolve()))

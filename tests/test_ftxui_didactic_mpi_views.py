"""Real MPI views: navigation must never mutate the paused capture."""
from pathlib import Path
import os
import signal
import sys
import tempfile
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal
from test_ftxui_mpi import text,wait,workers
from test_ftxui_responsive import resize

def enter(t,path,p):
    t.send('3');t.send('m');t.send('3');t.send('c')
    t.send('\x1b[H'+'\x1b[3~'*10+str(p)+'\r')
    t.send('C');t.send('m');t.send(str(path)+'\r')
    t.send('\x1b[B'*5+'\r')
    wait(t,lambda:'DIDÁCTICA MPI' in text(t) and 'capturada 1' in text(t))
    t.send(' ')
    assert 'En pausa' in text(t),text(t)

def check(binary,root):
    t=Terminal(binary,root,120,40)
    pids=set()
    try:
        enter(t,Path(root)/'patterns/blinker.txt',2)
        pids.update(workers(t.process.pid));assert len(pids)==2
        assert 'R0 · memoria privada' in text(t) and 'R1 · memoria privada' in text(t),text(t)
        t.send('vv')
        assert 'ENVÍA R0' in text(t) and 'Contenido recibido: 00100' in text(t),text(t)
        first=text(t);t.send('\x1b[F')
        assert 'S=1 · halo recibido de R1' in text(t) and 'Regla B3/S23' in text(t),text(t)
        assert 'Resultado generación 1' in text(t) and 'Líneas 1-' not in text(t),text(t)
        assert first!=text(t)
        t.send(']');t.send('\x1b[H')
        assert 'rank R1' in text(t) and 'ENVÍA R1 -> R0' in text(t),text(t)
        t.send('[');resize(t,50,24);t.read()
        assert 'Visible G0' in text(t) and 'En pausa' in text(t),text(t)
        assert 'N paso' in text(t) and 'Esp' in text(t) and 'pausa' in text(t),text(t)
        # Scroll physical rows, all the way to the final sample, after resize.
        t.send('\x1b[F');assert 'población global 3' in text(t),text(t)
        bottom=text(t);t.send('\x1b[H');assert bottom!=text(t) and 'EXPLICACIÓN' in text(t),text(t)
        t.send('v');assert 'Vista Global' in text(t),text(t)
        t.send('\x1b[B'*3);assert 'R0 fila 0' in text(t),text(t)
        t.send('v');t.send('\x1b[F');assert 'Población propia' in text(t),text(t)
        assert 'capturada 1' in text(t) and 'Etapa 1/6' in text(t),text(t)
        assert set(workers(t.process.pid))==pids,'navigation replaced session'
        resize(t,40,18);t.read()
        assert 'N paso' in text(t) and 'En pausa' in text(t),text(t)
        t.send('\x1b[F');assert 'Líneas' in text(t) and 'Población propia' in text(t),text(t)
        t.send('n');assert 'Etapa 2/6' in text(t) and 'En pausa' in text(t),text(t)
        resize(t,160,45);t.read();assert 'Intercambio 1' in text(t) and 'capturada 1' in text(t),text(t)
        t.send('++++');t.send(' ')
        wait(t,lambda:'capturada 2' in text(t),timeout=8)
        t.send(' ');assert 'En pausa' in text(t),text(t)
        t.send('r');wait(t,lambda:'capturada 1' in text(t) and 'Etapa 1/6' in text(t))
        assert 'En pausa' in text(t)
        # An actual worker failure must be visible and stop playback.
        pids.update(workers(t.process.pid))
        os.kill(workers(t.process.pid)[0],signal.SIGKILL)
        t.send('n'*6)
        wait(t,lambda:'ERROR:' in text(t))
        assert 'En pausa' in text(t),text(t)
        t.send('\x1b');t.send('0');assert t.process.wait(timeout=8)==0
        assert not any(Path('/proc/'+str(pid)).exists() for pid in pids)
        print('PASS P=2 real halos/cell/buffers, scroll last line, rank/view, resize, auto/manual/reset/error/close',flush=True)
    finally:
        if t.process.poll() is None:
            t.send('\x1b');t.send('0')
            try:t.process.wait(timeout=8)
            except Exception:pass
        t.close()

def empty_and_initial_error(binary,root):
    with tempfile.TemporaryDirectory(prefix='conway-mpi-view-') as d:
        path=Path(d)/'tiny.txt';path.write_text('1 1\n1\n')
        t=Terminal(binary,root,60,24)
        try:
            enter(t,path,2);t.send('v');t.send(']')
            assert 'Sin filas propias' in text(t),text(t)
            t.send('\x1b');wait(t,lambda:not workers(t.process.pid));t.send('C');t.send('m')
            path.write_text('129 129\n'+('0'*129+'\n')*129)
            t.send(str(path)+'\r');t.send('\x1b[H'+'\x1b[B'*5+'\r')
            wait(t,lambda:'ERROR:' in text(t))
            assert not workers(t.process.pid),text(t)
            t.send('\x1b');t.send('0');assert t.process.wait(timeout=8)==0
            print('PASS empty rank and rejected first capture diagnostic without MPI launch',flush=True)
        finally:
            if t.process.poll() is None:
                t.send('\x1b');t.send('0')
                try:t.process.wait(timeout=8)
                except Exception:pass
            t.close()

if __name__=='__main__':
    binary,root=[str(Path(p).resolve()) for p in sys.argv[1:]]
    check(binary,root);empty_and_initial_error(binary,root)

"""Existing views, new controller: notification, automatic/manual playback and MPI gates."""
from pathlib import Path
import os
import shlex
import sys
import tempfile
import time
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal
from test_ftxui_mpi import workers, wait, text
from test_ftxui_benchmark import move_to

def local(binary,root,size,engine="s"):
    t=Terminal(binary,root,*size)
    try:
        if engine!='s':t.send('3');t.send(engine)
        t.send('C');t.send('m');t.send(str(Path(root)/'patterns/blinker.txt')+'\r');t.send('u')
        t.send('d')
        wait(t,lambda:'MODO DIDÁCTICO' in text(t) and 'Reproduciendo' in text(t))
        t.send(' ')
        assert 'En pausa' in text(t) and 'Generación actual 0' in text(t),text(t)
        t.send('n');assert 'Generación actual 0' in text(t),text(t)
        t.send('n');wait(t,lambda:'Generación actual 1' in text(t) and 'En pausa' in text(t))
        before=t.raw;t.read();assert t.raw==before,'paused clock kept repainting'
        t.send('+');assert '2.0x' in text(t) and 'Generación actual 1' in text(t),text(t)
        t.send(' ')
        wait(t,lambda:'Generación actual 2' in text(t),timeout=6)
        t.send(' ');t.send('r');wait(t,lambda:'Generación actual 0' in text(t) and 'En pausa' in text(t))
        t.send('\x1b');assert 'MENÚ PRINCIPAL' in text(t),text(t)
        t.send('0');assert t.process.wait(timeout=5)==0
        print(f'PASS notified preparation, manual/automatic stages, speed, paused reset/exit {size}',flush=True)
    finally:
        if t.process.poll() is None:
            t.send('\x1b');t.send('\x1b');t.send('0')
            try:t.process.wait(timeout=5)
            except Exception:pass
        t.close()

def mpi_guard(fixture,launcher,worker,root):
    with tempfile.TemporaryDirectory(prefix='conway-didactic-ui-') as directory:
        wrapper=Path(directory)/'run';wrapper.write_text('#!/bin/sh\nexec '+shlex.join([fixture,launcher,worker])+'\n');wrapper.chmod(0o700)
        t=Terminal(str(wrapper),root,120,40)
        pids=set()
        try:
            wait(t,lambda:len(workers(t.process.pid))==1)
            pids=set(workers(t.process.pid))
            t.send('1');t.send('\r');move_to(t,'OpenMP');t.send(' ')
            move_to(t,'Ejecutar experimento');t.send('\r')
            assert 'sesión didáctica MPI antes de medir' in text(t),text(t)
            assert set(workers(t.process.pid))==pids
            t.send('\x1b');t.send('\x1b[B\r');move_to(t,'Ejecutar experimento');t.send('\r')
            assert 'sesión didáctica MPI antes de medir' in text(t),text(t)
            assert set(workers(t.process.pid))==pids
            t.send('\x1b');t.send('\x1b');t.send('c')
            assert set(workers(t.process.pid))==pids
            t.send('x')
            t.send('0');assert t.process.wait(timeout=5)==0
            for pid in pids:
                try:os.kill(pid,0)
                except ProcessLookupError:continue
                raise AssertionError(f'didactic rank {pid} survived app exit')
            print('PASS live MPI didactic session blocks both experiments/visualization, async exit cleans ranks',flush=True)
        finally:
            if t.process.poll() is None:
                t.send('\x1b');t.send('\x1b');t.send('0')
                try:t.process.wait(timeout=5)
                except Exception:pass
            t.close()

if __name__=='__main__':
    binary,fixture,launcher,worker,root=[str(Path(p).resolve()) for p in sys.argv[1:]]
    for size in [(120,40),(50,24)]:local(binary,root,size)
    local(binary,root,(80,24),'o')
    mpi_guard(fixture,launcher,worker,root)

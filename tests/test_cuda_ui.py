"""Real textual and FTXUI selection, configuration and normal playback."""
import os
import re
from pathlib import Path
import subprocess
import sys
import time
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal

binary,probe,enabled,root=sys.argv[1:]
mode=enabled
enabled=mode=='ON'
missing='GPU ausente' if mode=='HIDDEN' else 'build sin CUDA'
check=subprocess.run([probe,'--availability'],capture_output=True,text=True)
if mode=='ON' and check.returncode==77:
    print(check.stdout);sys.exit(77)
assert check.returncode==(0 if enabled else 77 if mode=='HIDDEN' else 2),(check.stdout,check.stderr)
os.chdir(root)
commands='C\nM\npatterns/blinker.txt\nU\nM\nG\n'
if enabled:
    commands+='R\n1\nV\nN\nB\n3\nX\nV\nN\nX\nQ\n'
else:
    commands+='X\nV\nN\nX\nV\nX\nQ\n'
text=subprocess.run([binary],input=commands,text=True,capture_output=True,timeout=20)
assert text.returncode==0,(text.stdout,text.stderr)
assert 'SIMULACIÓN NORMAL' in text.stdout and 'Generación 1' in text.stdout,text.stdout
if enabled:
    assert 'Motor seleccionado: CUDA' in text.stdout and 'Generación 2' in text.stdout,text.stdout
    assert 'Bloque 32x8' in text.stdout and 'GPU:' in text.stdout,text.stdout
else:
    assert missing in text.stdout and 'Motor: Secuencial' in text.stdout,text.stdout

# PTY uses the same production executable, no fake GPU or fixture board.
t=Terminal(binary,root,120,42)
def visible():return '\n'.join(t.screen().values())
def wait_for(value):
    end=time.monotonic()+6
    while value not in visible() and time.monotonic()<end:t.read(.03)
    assert value in visible(),(value,visible())
try:
    t.read();t.send('c');t.send('m');t.send('patterns/blinker.txt\n');t.send('u')
    t.send('m');t.send('g')
    if enabled:
        assert 'CUDA' in t.context(),visible()
        t.send('r');wait_for('BLOQUE CUDA');t.send('1')
        t.send('v');wait_for('SIMULACIÓN NORMAL');wait_for('Generación 0')
        t.send('n');wait_for('Generación 1');wait_for('Vivas 3')
        t.send('b');wait_for('BLOQUE CUDA');t.send('\x1b');wait_for('Generación 1');wait_for('Bloque 8x8')
        t.send('b');wait_for('BLOQUE CUDA');t.send('3');wait_for('Generación 1');wait_for('Bloque 32x8')
        t.send('x');t.send('v');wait_for('Generación 1');wait_for('Bloque 32x8')
        t.send('m');t.send('o');wait_for('Generación 1');assert 'OpenMP' in visible(),visible()
        t.send('m');t.send('g');wait_for('Generación 1');wait_for('Bloque 32x8')
        t.send('n');wait_for('Generación 2')
        t.send('r');wait_for('Generación 0')
        t.send('p');t.send('p')
        paused=re.search(r'Generación (\d+)',visible())
        assert paused and int(paused[1])>0,visible()
        t.read();assert re.search(r'Generación (\d+)',visible())[1]==paused[1],visible()
    else:
        wait_for(missing);assert 'Secuencial' in t.context(),visible()
        t.send('\x1b');t.send('v');wait_for('SIMULACIÓN NORMAL');t.send('n');wait_for('Generación 1')
        t.send('m');t.send('g');wait_for(missing);t.send('\x1b');wait_for('Generación 1')
    t.send('x');t.send('q');t.process.wait(timeout=5)
    assert t.process.returncode==0
finally:t.close()

# Minimum supported terminal: the numeric grid must remain visible, not just
# its label truncated by a wrapped metadata line.
t=Terminal(binary,root,40,18)
try:
    for key in ['c','m','patterns/blinker.txt\n','u','m','g']:t.send(key)
    if not enabled:t.send('\x1b')
    t.send('v');t.send('n');t.assert_fits()
    small='\n'.join(t.screen().values())
    assert 'Generación 1' in small,small
    if enabled:assert 'grid 1x1' in small and 'Bloque 16x16' in small and '256 h/bloque' in small,small
    t.send('x');t.send('q');t.process.wait(timeout=5)
finally:t.close()

# GPU absent is a visible rejection in an ON application; never select CPU implicitly.
if enabled:
    env=os.environ.copy();env['CUDA_VISIBLE_DEVICES']='-1'
    absent=subprocess.run([binary],input='M\nG\nX\nQ\n',capture_output=True,text=True,env=env,timeout=15)
    assert absent.returncode==0 and 'GPU ausente' in absent.stdout and 'Motor seleccionado: CUDA' not in absent.stdout,absent.stdout
print('PASS textual/FTXUI CUDA selection, normal board, block changes, persistence and unavailable diagnostics')

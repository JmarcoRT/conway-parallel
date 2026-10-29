"""Real CUDA didactic: manual exploration versus generation, presets and resize."""
import fcntl
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal
binary,probe,mode,root=sys.argv[1:]
check=subprocess.run([probe,'--availability'],capture_output=True,text=True)
if mode=='ON' and check.returncode==77:
    print(check.stdout);sys.exit(77)
assert check.returncode==(0 if mode=='ON' else 77 if mode=='HIDDEN' else 2),(check.stdout,check.stderr)

def flat(t):return ' '.join(' '.join(t.screen().values()).split())
def wait(t,value):
    end=time.monotonic()+6
    while value not in flat(t) and time.monotonic()<end:t.read(.03)
    assert value in flat(t),(value,flat(t))
def generation(t):
    match=re.search(r'G(\d+) · B',flat(t));assert match,flat(t);return int(match[1])
warning='Exploración conceptual; no representa el orden de ejecución GPU'
with tempfile.TemporaryDirectory(prefix='conway-cuda-didactic-') as directory:
    pattern=Path(directory)/'example35x19.txt'
    pattern.write_text('19 35\n'+'\n'.join(''.join('1' if (r*35+c)*17%7<3 else '0' for c in range(35)) for r in range(19))+'\n')
    t=Terminal(binary,root,120,42)
    try:
        for key in ['c','m',str(pattern)+'\n','u','m','g']:t.send(key)
        if mode!='ON':
            wait(t,'GPU ausente' if mode=='HIDDEN' else 'build sin CUDA')
            assert warning not in flat(t),flat(t)
            t.send('\x1b');t.send('q');assert t.process.wait(timeout=5)==0
            print('PASS didactic CUDA unavailable: selection rejected with diagnosis');sys.exit(0)
        t.send('v');t.send('p');t.send('d');wait(t,warning)
        paused=generation(t);t.read();assert generation(t)==paused,'Normal autoplay survived didactic entry'
        for key in ['n','n',' ','p','+','-']:t.send(key)
        assert generation(t)==paused,'Manual exploration/disabled playback advanced generation'
        t.send(']'*5);t.send('l'*15);t.send('j'*15)
        t.send('v');wait(t,'blockIdx=(2,1)');wait(t,'threadIdx=(15,15)')
        wait(t,'columna = 2 * 16 + 15 = 47');wait(t,'fila = 1 * 16 + 15 = 31')
        wait(t,'Posición exterior');wait(t,'barreras');assert generation(t)==paused
        t.send('g');wait(t,f'G{paused+1} · B');assert generation(t)==paused+1
        t.send('b');wait(t,'BLOQUE CUDA');t.send('\x1b');assert generation(t)==paused+1
        t.send('b');t.send('3');wait(t,'B32x8');wait(t,'grid 2x3');wait(t,'Selección ajustada')
        wait(t,'columna = 1 * 32 + 2 = 34');wait(t,'fila = 2 * 8 + 2 = 18')
        t.send('b');t.send('1');wait(t,'B8x8');wait(t,'grid 5x3')
        t.send('b');t.send('2');wait(t,'B16x16');wait(t,'grid 3x2')
        assert generation(t)==paused+1
        t.send('x');t.send('d');wait(t,warning);assert generation(t)==paused+1
        wait(t,'CPU con regla compartida')
        t.send('x');t.send('v');wait(t,f'Generación {paused+1}')
        t.send('d');wait(t,warning);t.send('r');wait(t,'G0 · B16x16')
        wait(t,'blockIdx=(0,0)');wait(t,'threadIdx=(0,0)')
        # Show board before resize, then verify warning, essential data and grid.
        t.send('v')
        t.columns,t.rows=40,18
        t.raw=b'' # Discard the previous raster size, as in existing resize tests.
        fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',18,40,0,0))
        os.kill(t.process.pid,signal.SIGWINCH);t.read();t.assert_fits()
        wait(t,warning);wait(t,'grid 3x2');wait(t,'CPU B3/S23')
        assert any(x in flat(t) for x in ['┼','┃','║']),flat(t)
        t.send(']'*5);t.send('l'*15);t.send('j'*15)
        t.send('b');t.send('3');wait(t,'B32x8');wait(t,'Ajuste:')
        assert any(x in flat(t) for x in ['┼','┃','║']),flat(t)
        t.send('b');t.send('2');wait(t,'B16x16');t.send('r');wait(t,'G0 · B16x16')
        t.send('v');seen=flat(t)
        for _ in range(16):t.send('\x1b[6~');seen+=' '+flat(t)
        assert 'Predicción CPU con regla compartida' in seen,seen
        assert generation(t)==0
        # Resize below supported minimum, restore: no selection/generation loss.
        for w,h in [(35,15),(120,42)]:
            t.columns,t.rows=w,h
            t.raw=b''
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,0,0))
            os.kill(t.process.pid,signal.SIGWINCH);t.read()
        wait(t,warning);assert generation(t)==0
        t.send('x');t.send('q');assert t.process.wait(timeout=5)==0
        print('PASS 35x19 CUDA didactic PTY: manual exploration, true G, presets, persistence, reset, 40x18 and resize')
    finally:t.close()

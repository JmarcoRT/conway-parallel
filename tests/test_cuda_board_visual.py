"""Presentation/controls only: no generation advance or backend comparisons."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal,ROW
binary,probe,mode,root=sys.argv[1:]
check=subprocess.run([probe,'--availability'],capture_output=True,text=True)
if mode=='ON' and check.returncode==77:print(check.stdout);sys.exit(77)
assert check.returncode==(0 if mode=='ON' else 2),(check.stdout,check.stderr)
def flat(t):return ' '.join(' '.join(t.screen().values()).split())
def wait(t,value):
    end=time.monotonic()+6
    while value not in flat(t) and time.monotonic()<end:t.read(.03)
    assert value in flat(t),(value,flat(t))
def board(t):
    lines=t.screen()
    tops=[(y,s.index('┌'),s.index('┐')) for y,s in lines.items() if '┌' in s and '┐' in s]
    assert tops,flat(t)
    y,x,x1=tops[-1]
    bottom=next((r for r in sorted(lines) if r>y and len(lines[r])>x1 and lines[r][x]=='└' and lines[r][x1]=='┘'),None)
    assert bottom is not None,flat(t)
    return (x,x1,y,bottom),tuple(lines[r][x:x1+1] for r in range(y,bottom+1))
def capture(t,name):
    directory=os.environ.get('CONWAY_VISUAL_CAPTURE')
    if not directory:return
    rows={}
    for m in ROW.finditer(t.raw.decode('utf-8',errors='replace')):rows[int(m[1])]=m[2].replace('\r','').replace('\n','')
    Path(directory,name+'.ansi').write_text('\x1b[0m'+'\n'.join(rows.get(r,'') for r in range(1,t.rows+1)))
with tempfile.TemporaryDirectory(prefix='conway-board-visual-') as directory:
    for w,h in [(5,5),(35,19)]:
      pattern=Path(directory)/f'{w}x{h}.txt'
      pattern.write_text(f'{h} {w}\n'+'\n'.join(''.join('1' if c==2 and r in (1,2,3) else '0' for c in range(w)) for r in range(h))+'\n')
      for mono in (False,True):
        executable=binary
        if mono:
            wrapper=Path(directory)/'no-color';wrapper.write_text('#!/bin/sh\nexport NO_COLOR=1\nexec '+shlex.quote(binary)+'\n');wrapper.chmod(0o700);executable=str(wrapper)
        t=Terminal(executable,root,120 if w==5 else 180,42 if w==5 else 58)
        try:
            for key in ['c','m',str(pattern)+'\n','u','m','g']:t.send(key)
            if mode!='ON':
                wait(t,'build sin CUDA');t.send('\x1b');t.send('q');assert t.process.wait(timeout=5)==0
                print('PASS CUDA OFF selection remains unavailable with diagnostic');sys.exit(0)
            t.send('d');wait(t,'Exploración conceptual');wait(t,'G0 · B16x16')
            wait(t,'@0');capture(t,f'pty-{w}-dead-'+('mono' if mono else 'color'))
            assert '×' not in ''.join(board(t)[1]),flat(t)
            t.send('lljj');wait(t,'@1');wait(t,':1');wait(t,':0')
            capture(t,f'pty-{w}-live-'+('mono' if mono else 'color'))
            before=board(t);t.send('a');wait(t,'Cobertura A');assert board(t)==before,'A changed board bounds/pixels'
            capture(t,f'pty-{w}-coverage-'+('mono' if mono else 'color'))
            t.send('l'*13+'j'*13)
            if w==35:t.send(']'*5)
            wait(t,'Posición exterior');assert '@1' not in ''.join(board(t)[1]) and '@0' not in ''.join(board(t)[1])
            capture(t,f'pty-{w}-exterior-'+('mono' if mono else 'color'))
            for preset,label in [('1','B8x8'),('3','B32x8'),('2','B16x16')]:
                t.send('b');t.send(preset);wait(t,label);wait(t,'G0 · B')
            t.send('x');t.send('d');wait(t,'Cobertura A');wait(t,'G0 · B16x16')
            # Small layout: board stays present; A remains a fixed-height compact legend.
            import fcntl,struct,termios,signal
            t.columns,t.rows=40,18;t.raw=b''
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',18,40,0,0));os.kill(t.process.pid,signal.SIGWINCH);t.read();t.assert_fits()
            wait(t,'Exploración conceptual');wait(t,'A:+');before=board(t)
            capture(t,f'pty-{w}-small-'+('mono' if mono else 'color'))
            t.send('a');assert board(t)==before,'Compact coverage displaced board'
            t.send('a');wait(t,'A:+');t.send('v');wait(t,'Cobertura A');t.send('v')
            wait(t,'G0 · B16x16');t.send('x');t.send('q');assert t.process.wait(timeout=5)==0
            print(f'PASS {w}x{h}, mono={mono}: live/dead/exterior, A fixed board, presets, persistence, 40x18; G unchanged',flush=True)
        finally:t.close()

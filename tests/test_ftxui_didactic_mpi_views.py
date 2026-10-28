"""Real MPI views: navigation must never mutate the paused capture."""
from pathlib import Path
import os
import re
import shlex
import signal
import sys
import tempfile
sys.dont_write_bytecode=True
from test_ftxui_header import Terminal, ROW
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

def visual(t):
    """Decode the actual PTY fills/attributes, never infer cells from labels.

    O/. contain no product glyphs: they are a test-only transcription of the
    green/inverse fill and neutral fill. ? transcribes an underlined hidden cell.
    """
    lines={}
    for match in ROW.finditer(t.raw.decode('utf-8',errors='replace')):
        pixels=[];bg=None;inverse=False;underline=False
        for token in re.findall(r'\x1b\[[0-?]*[ -/]*[@-~]|[^\x1b]',match[2]):
            if token.startswith('\x1b'):
                if not token.endswith('m'):continue
                codes=[int(v or 0) for v in token[2:-1].split(';')];i=0
                while i<len(codes):
                    c=codes[i]
                    if c==0:bg=None;inverse=False;underline=False
                    elif c==7:inverse=True
                    elif c==27:inverse=False
                    elif c==4:underline=True
                    elif c==24:underline=False
                    elif c==49:bg=None
                    elif c in (38,48) and i+1<len(codes):
                        n=4 if codes[i+1]==2 else 2
                        if c==48:bg=tuple(codes[i+2:i+n+1])
                        i+=n
                    i+=1
            elif token not in '\r\n':pixels.append((token,bg,inverse,underline))
        line=''.join(p[0] for p in pixels)
        # Every interior must be blank, including selected/hidden/message cells.
        for cell in re.finditer(r'[│║┃┆╎]([ .OCv?]+)(?=[│║┃┆╎])',line):
            assert not any(v in cell[1] for v in '.OCv?'),line
        output=[];start=0
        for end,pixel in enumerate(pixels):
            if pixel[0] not in '│║┃┆╎':continue
            segment=pixels[start:end]
            if segment and all(p[0]==' ' for p in segment):
                p=segment[0];value='?' if p[3] else 'O' if p[1]==(98,232,137) or p[2] else '.'
                output.append(value+' '*(len(segment)-1))
            else:output.append(''.join(p[0] for p in segment))
            output.append(pixel[0]);start=end+1
        output.append(''.join(p[0] for p in pixels[start:]))
        lines[int(match[1])]=''.join(output)
    return '\n'.join(lines.values())

def collect(t):
    """Read overlapping physical viewports, including the actual final row."""
    t.send('\x1b[H');screens=[]
    for _ in range(150):
        view=visual(t);screens.append(view)
        match=re.search(r'Líneas (\d+)-(\d+)/(\d+)',view)
        assert match,view
        first,last,total=map(int,match.groups())
        if last==total:break
        t.send('\x1b[B'*max(1,last-first))
    else:raise AssertionError('scroll did not reach last physical row')
    return '\n'.join(screens)

def cells(view,label,width=5):
    for line in view.splitlines():
        if label not in line:continue
        tail=line.split(label,1)[1]
        found=re.findall(r'[│║┃┆╎]([O.?]) *(?=[│║┃┆╎])',tail)
        if len(found)>=width:return ''.join(found[:width])
    raise AssertionError(f'missing grid row {label}:\n{view}')

def check(binary,root):
    t=Terminal(binary,root,120,40);pids=set()
    try:
        enter(t,Path(root)/'patterns/blinker.txt',2)
        pids.update(workers(t.process.pid));assert len(pids)==2
        initial=visual(t)
        for label,expected in [('R0 g0','.....'),('R0 g1','..O..'),('R0 g2','..O..'),('R1 g3','..O..'),('R1 g4','.....')]:
            assert cells(initial,label)==expected,initial
        assert b'48;2;98;232;137m' in t.raw,'living cells lost canonical green'
        assert b'48;2;232;158;90m' in t.raw and b'48;2;90;143;232m' in t.raw,'rank colors missing'
        initial_all=collect(t)
        assert 'NEXT' not in initial_all and 'Swap capturado' not in initial_all and 'Regla B3' not in initial_all
        assert cells(initial_all,'H↓ no vigente')=='?????',initial_all
        t.send('n');t.send('vv')  # First call across ALL ranks: only R1 -> R0 at P=2.
        first=collect(t)
        assert 'ENVÍA R1 -> R0' in first and 'ENVÍA R0 -> R1' not in first,first
        assert cells(first,'fila g3')=='..O..',first
        assert 'Regla B3' not in first and 'Swap capturado' not in first and 'NEXT' not in first
        t.send('v');t.send('v');first_local=collect(t)
        assert cells(first_local,'H de R1 g3')=='..O..',first_local
        assert 'Composición didáctica' in first_local and 'snapshot intermedio' in first_local
        assert 'NEXT' not in first_local
        t.send('n');t.send('v');second=collect(t)
        assert 'ENVÍA R0 -> R1' in second and 'ENVÍA R1 -> R0' not in second,second
        assert cells(second,'fila g2')=='..O..',second
        t.send('n');cell=collect(t)
        assert 'S=1 · halo recibido de R1' in cell and 'Regla B3/S23 capturada: antes 1 · vecinos 2 -> 1' in cell,cell
        t.send('v');t.send('v');selected=collect(t)
        assert 'NEXT · solo célula evaluada' in selected and 'Swap capturado' not in selected,selected
        assert '║' in selected and '┃' in selected,selected
        # The same local row occurs twice: CURRENT and the selectively revealed NEXT.
        assert re.search(r'│\? +│\? +│O +│\? +│\?',selected),selected
        t.send('n');buffers=collect(t)
        assert 'NEXT calculado G1' in buffers and 'Swap capturado: sí' in buffers,buffers
        assert re.search(r'│\. +│O +│O +│O +│\.',buffers),buffers
        t.send('n');result=collect(t)
        assert 'CURRENT nuevo G1' in result and 'no vigente' in result
        t.send('v');result_explain=collect(t)
        assert 'Resultado generación 1' in result_explain and 'población global 3' in result_explain
        # Paused navigation, resizing and scrolling cannot compute another generation.
        t.send('v');resize(t,50,24);t.read()
        small=collect(t)
        assert cells(small,'R0 g2')=='.OOO.',small
        assert 'capturada 1' in text(t) and 'Etapa 6/6' in text(t)
        t.send('v');t.send(']');local=collect(t)
        assert 'R1 · memoria privada' in local and 'CURRENT nuevo G1' in local,local
        t.send('[');resize(t,40,18);t.read()
        compact=collect(t)
        assert 'N paso' in compact and 'En pausa' in compact and 'CURRENT nuevo G1' in compact,compact
        assert set(workers(t.process.pid))==pids,'navigation replaced session'
        resize(t,160,45);t.read();t.send('++++');t.send(' ')
        wait(t,lambda:'capturada 2' in text(t),timeout=8)
        t.send(' ');assert 'En pausa' in text(t)
        t.send('r');wait(t,lambda:'capturada 1' in text(t) and 'Etapa 1/6' in text(t))
        assert 'En pausa' in text(t)
        pids.update(workers(t.process.pid));os.kill(workers(t.process.pid)[0],signal.SIGKILL)
        t.send('n'*6);wait(t,lambda:'ERROR:' in text(t));assert 'En pausa' in text(t)
        t.send('\x1b');t.send('0');assert t.process.wait(timeout=8)==0
        assert not any(Path('/proc/'+str(pid)).exists() for pid in pids)
        print('PASS visible grid pattern/colors, stage gates, directed messages/halos, partial NEXT, rank/scroll/resize, auto/reset/error/cleanup',flush=True)
    finally:
        if t.process.poll() is None:
            t.send('\x1b');t.send('0')
            try:t.process.wait(timeout=8)
            except Exception:pass
        t.close()

def no_color_stages(binary,root):
    with tempfile.TemporaryDirectory(prefix='conway-no-color-') as d:
        wrapper=Path(d)/'run';wrapper.write_text('#!/bin/sh\nexport NO_COLOR=1\nexec '+shlex.quote(binary)+'\n');wrapper.chmod(0o700)
        t=Terminal(str(wrapper),root,80,30)
        try:
            enter(t,Path(root)/'patterns/blinker.txt',2)
            assert cells(collect(t),'R0 g2')=='..O..'
            t.send('v')
            for stage in range(6):
                local=collect(t)
                if stage==0:assert cells(local,'H↓ no vigente')=='?????',local
                if stage in (1,2):
                    assert cells(local,'H de R1 g3')=='..O..',local
                    t.send('v');messages=collect(t)
                    assert cells(messages,'fila g'+('3' if stage==1 else '2'))=='..O..',messages
                    t.send('v');t.send('v')
                if stage<3:assert 'NEXT' not in local and 'Swap capturado' not in local
                if stage==3:
                    assert '║' in local and '┃' in local and re.search(r'│\? +│\? +│O +│\? +│\?',local),local
                if stage==4:assert re.search(r'│\. +│O +│O +│O +│\.',local),local
                if stage==5:assert 'CURRENT nuevo G1' in local and cells(local,'H↓ no vigente')=='?????',local
                if stage<5:t.send('n')
            assert b'48;2;98;232;137m' not in t.raw
            t.send('\x1b');t.send('0');assert t.process.wait(timeout=8)==0
            print('PASS all stages without color: inverse lives, hidden underlining, selection/neighbor borders, message fills',flush=True)
        finally:
            if t.process.poll() is None:
                t.send('\x1b');t.send('0')
                try:t.process.wait(timeout=8)
                except Exception:pass
            t.close()

def empty_and_initial_error(binary,root):
    with tempfile.TemporaryDirectory(prefix='conway-mpi-view-') as d:
        path=Path(d)/'tiny.txt';path.write_text('1 21\n1'+'0'*19+'1\n')
        wrapper=Path(d)/'no-color';wrapper.write_text('#!/bin/sh\nexport NO_COLOR=1\nexec '+shlex.quote(binary)+'\n');wrapper.chmod(0o700)
        t=Terminal(str(wrapper),root,60,24)
        try:
            enter(t,path,2)
            before=collect(t);assert cells(before,'R0 g0',13)=='O'+'.'*12,before
            assert b'48;2;98;232;137m' not in t.raw,'NO_COLOR was ignored'
            t.send('\x1b[C'*100);after=collect(t)
            assert 'Columnas 0-' in before and 'Columnas 0-' not in after and cells(after,'R0 g0',13)=='.'*12+'O',after
            t.send('v');t.send(']')
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
    check(binary,root);no_color_stages(binary,root);empty_and_initial_error(binary,root)

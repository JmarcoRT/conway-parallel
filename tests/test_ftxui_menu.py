"""Inspecciona RGB, ancho y foco de menus reales sobre una PTY."""
from pathlib import Path
import re
import shlex
import sys
import tempfile
sys.dont_write_bytecode = True
from test_ftxui_header import ANSI, ROW, Terminal

CYAN = (83, 191, 208)
CHARCOAL = (25, 32, 38)
SGR = re.compile(r"\x1b\[([0-9;]*)m")


def pixels(row):
    foreground = background = None
    result = []
    start = 0
    for match in SGR.finditer(row):
        result.extend((char, foreground, background) for char in row[start:match.start()])
        codes = [int(value or 0) for value in match[1].split(";")]
        index = 0
        while index < len(codes):
            code = codes[index]
            if code == 0:
                foreground = background = None
            elif code in (38, 48) and codes[index + 1:index + 2] == [2]:
                color = tuple(codes[index + 2:index + 5])
                if code == 38:
                    foreground = color
                else:
                    background = color
                index += 4
            elif code == 39:
                foreground = None
            elif code == 49:
                background = None
            index += 1
        start = match.end()
    result.extend((char, foreground, background) for char in row[start:])
    return result


def expect_focus(terminal, label, full_width=True):
    rows = {}
    for match in ROW.finditer(terminal.raw.decode("utf-8", errors="replace")):
        rows[int(match[1])] = match[2]
    matches = [row for row in rows.values() if "> " + label in ANSI.sub("", row)]
    assert len(matches) == 1, (label, terminal.screen())
    row_pixels = pixels(matches[0])
    focused = [(index, pixel) for index, pixel in enumerate(row_pixels) if pixel[2] == CYAN]
    assert focused, (label, matches[0])
    assert all(pixel[1] == CHARCOAL for _, pixel in focused), (label, focused)
    assert len(focused) > len(label) + 2, (label, focused)  # Incluye relleno.
    if full_width:
        assert len(focused) == terminal.columns - 2, (label, len(focused))
    # Un unico menu conserva el foco; las selecciones inactivas no son cian.
    colored_rows = [row for row in rows.values() if any(pixel[2] == CYAN for pixel in pixels(row))]
    assert len(colored_rows) == 1, (label, len(colored_rows))


def expect_identity_heading(terminal, label):
    rows = {}
    for match in ROW.finditer(terminal.raw.decode("utf-8", errors="replace")):
        rows[int(match[1])] = match[2]
    for row in rows.values():
        row_pixels = pixels(row)
        text = "".join(pixel[0] for pixel in row_pixels)
        if label in text:
            start = text.index(label)
            assert all(pixel[1] == CYAN for pixel in row_pixels[start:start + len(label)]), row
            return
    raise AssertionError((label, terminal.screen()))


def raw_rows(t):
    return {int(m[1]):m[2] for m in ROW.finditer(t.raw.decode('utf-8',errors='replace'))}

DESCRIPTIONS=['Comprender cómo calcula cada motor','Comparar rendimiento y escalabilidad',
    'Explorar, reproducir y elegir un patrón','Secuencial, OpenMP o MPI','Recursos según el motor','']
LABELS=['Visualización didáctica','Benchmark','Catálogo de patrones','Seleccionar motor','Configurar recursos','Salir']

def contrast(a,b):
    def luminance(rgb):
        c=[v/255 for v in rgb]
        c=[v/12.92 if v<=0.04045 else ((v+0.055)/1.055)**2.4 for v in c]
        return sum(v*w for v,w in zip(c,[.2126,.7152,.0722]))
    x,y=sorted([luminance(a),luminance(b)])
    return (y+.05)/(x+.05)

def block(t,index,descriptions):
    rows=raw_rows(t)
    line=next(y for y,row in rows.items() if '> '+LABELS[index] in ANSI.sub('',row))
    data=pixels(rows[line]);fill=[x for x,p in enumerate(data) if p[2]==CYAN]
    assert fill and len(fill)>len(LABELS[index])+2,(fill,t.screen())
    assert fill==list(range(fill[0],fill[-1]+1)),fill
    assert all(data[x][1]==CHARCOAL for x in fill),rows[line]
    assert contrast(CHARCOAL,CYAN)>=4.5
    expected=2 if descriptions and DESCRIPTIONS[index] else 1
    highlighted=[y for y,row in rows.items() if any(p[2]==CYAN for p in pixels(row))]
    assert highlighted==list(range(line,line+expected)),(highlighted,t.screen())
    if expected==2:
        assert DESCRIPTIONS[index] in ANSI.sub('',rows[line+1])
        secondary=pixels(rows[line+1]);other=[x for x,p in enumerate(secondary) if p[2]==CYAN]
        assert other==fill,'description/background does not cover same full block width'
        assert all(secondary[x][1]==(40,55,65) for x in fill),rows[line+1]
        assert contrast((40,55,65),CYAN)>=4.5
        assert '\x1b[2m' not in rows[line+1],'selected description must keep contrast'
    # Other visible descriptions use a legible secondary gray, not dim cyan.
    for i,description in enumerate(DESCRIPTIONS):
        if not description or i==index or not descriptions:continue
        row=next(row for row in rows.values() if description in ANSI.sub('',row))
        tokens=pixels(row);plain=''.join(p[0] for p in tokens);start=plain.index(description)
        assert all(p[1]==(168,180,188) for p in tokens[start:start+len(description)]),row
    return line,fill

def check(binary,cwd,columns,rows):
    terminal=Terminal(binary,cwd,columns,rows)
    try:
        descriptions=rows>=30 and columns>=64
        for index in range(6):
            line,fill=block(terminal,index,descriptions)
            if columns>88:
                assert abs((fill[0]+fill[-1])-(columns-1))<=1,(columns,fill)
                assert len(fill)==82,fill
            terminal.send('\t')
        expect_identity_heading(terminal,'MODOS PRINCIPALES')
        if rows>=24:
            screen=terminal.screen();title=next(y for y,row in screen.items() if 'MENÚ PRINCIPAL' in row)
            bottom=next(y for y,row in screen.items() if y>title and '╰' in row)
            exit_row=next(y for y,row in screen.items() if 'Salir' in row)
            assert bottom-exit_row<=2,(exit_row,bottom,screen)
            assert bottom-(title-1)+1<=23,(title,bottom,screen)
        terminal.send('c');terminal.send('\x1b[B');terminal.send('\r')
        assert 'Sin cargar' in terminal.context()
        terminal.send('u');terminal.expect('Patrón: willow.txt','6 × 4')
        terminal.send('q');assert terminal.process.wait(timeout=5)==0
        print(f'PASS main blocks/full width/contrast/compact panel/navigation {columns}x{rows}',flush=True)
    finally:terminal.close()

def resizing(binary,cwd,no_color=False):
    from test_ftxui_responsive import resize
    with tempfile.TemporaryDirectory(prefix='main-menu-style-') as directory:
        if no_color:
            wrapper=Path(directory)/'run';wrapper.write_text('#!/bin/sh\nexport NO_COLOR=1\nexec '+shlex.quote(binary)+'\n');wrapper.chmod(0o700)
            binary=str(wrapper)
        terminal=Terminal(binary,cwd,160,50)
        try:
            terminal.send('\t') # Benchmark remains selected across all sizes.
            for width,height in [(160,50),(80,30),(50,24),(40,18),(120,40),(240,60)]:
                resize(terminal,width,height);terminal.read();terminal.assert_fits()
                rows=raw_rows(terminal)
                assert any('> Benchmark' in ANSI.sub('',row) for row in rows.values())
                assert 'Q salir' in '\n'.join(ANSI.sub('',row) for row in rows.values())
                if no_color:
                    selected=next(y for y,row in rows.items() if '> Benchmark' in ANSI.sub('',row))
                    assert '\x1b[1m' in rows[selected] and '\x1b[7m' in rows[selected],rows[selected]
                    if height>=30:
                        assert 'Comparar rendimiento y escalabilidad' in ANSI.sub('',rows[selected+1])
                        assert '\x1b[7m' in rows[selected+1] and '\x1b[2m' not in rows[selected+1],rows[selected+1]
                        unselected=next(row for row in rows.values() if DESCRIPTIONS[0] in ANSI.sub('',row))
                        assert '\x1b[2m' in unselected,unselected
                    assert not any(p[2]==CYAN for row in rows.values() for p in pixels(row))
                else:block(terminal,1,height>=30)
            terminal.send('q');assert terminal.process.wait(timeout=5)==0
            print(f'PASS main resize and selected block preservation, NO_COLOR={no_color}',flush=True)
        finally:terminal.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="conway-menu-") as cwd:
        patterns = Path(cwd) / "patterns"
        patterns.mkdir()
        (patterns / "cedar.txt").write_text("3 7\n" + "0000000\n" * 3)
        (patterns / "willow.txt").write_text("6 4\n" + "0000\n" * 6)
        for size in [(160, 50), (120, 40), (80, 30), (80, 24), (50, 24), (40, 18)]:
            check(binary, cwd, *size)

        resizing(binary,cwd)
        resizing(binary,cwd,True)

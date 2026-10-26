"""An open visualization MPI session must block both configurable experiments."""
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_mpi import workers, wait, text
from test_ftxui_benchmark import move_to

terminal = Terminal(sys.argv[1], sys.argv[2], 120, 40)
try:
    terminal.send('C'); terminal.send('m')
    terminal.send(str(Path(sys.argv[2]) / 'patterns/blinker.txt') + '\r')
    terminal.send('3'); terminal.send('m'); terminal.send('2')
    wait(terminal, lambda: len(workers(terminal.process.pid)) == 1 and 'Pausado' in text(terminal))
    pids = set(workers(terminal.process.pid))
    terminal.send('\x1b'); terminal.send('1'); terminal.send('\r')
    move_to(terminal, 'Ejecutar experimento'); terminal.send('\r')
    assert 'Abandona la sesión MPI con Q antes de medir' in text(terminal), text(terminal)
    assert 'COMPARACIÓN' in text(terminal).upper(), text(terminal)
    assert set(workers(terminal.process.pid)) == pids
    terminal.send('\x1b'); terminal.send('\x1b[B\r')
    move_to(terminal, 'Ejecutar experimento'); terminal.send('\r')
    assert 'Abandona la sesión MPI con Q antes de medir' in text(terminal), text(terminal)
    assert 'ESCALABILIDAD' in text(terminal).upper(), text(terminal)
    assert set(workers(terminal.process.pid)) == pids
    terminal.send('\x1b'); terminal.send('\x1b'); terminal.send('2'); terminal.send('q')
    wait(terminal, lambda: not workers(terminal.process.pid))
    terminal.send('0'); assert terminal.process.wait(timeout=5) == 0
    print('PASS open MPI visualization blocks comparison and scaling; no experiment launched')
finally:
    if terminal.process.poll() is None:
        terminal.send('\x1b'); terminal.send('\x1b'); terminal.send('2'); terminal.send('q')
    terminal.close()

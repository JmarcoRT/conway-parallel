"""Exercise readable MPI failures and technical details through a real terminal."""
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_mpi import text, wait
from test_ftxui_responsive import resize


def check(binary, root, diagnostic, resource_error):
    with tempfile.TemporaryDirectory(prefix='conway-error-') as directory:
        app = Path(directory) / 'conway'
        shutil.copy2(binary, app)
        worker = Path(directory) / 'conway_mpi_worker'
        worker.write_text('#!/bin/sh\ncat >&2 <<\'DIAGNOSTIC\'\n' + diagnostic + '\nDIAGNOSTIC\nexit 1\n')
        worker.chmod(0o755)
        terminal = Terminal(str(app), root, 100, 35)
        try:
            terminal.send('m'); terminal.send('m'); terminal.send('r')
            terminal.send('\x1b[H' + '\x1b[3~' * 10 + '2\r')
            terminal.send('C'); terminal.send('m')
            terminal.send(str(Path(root) / 'patterns/pulsar.txt') + '\r')
            terminal.send('u'); terminal.send('d')
            wait(terminal, lambda: 'CAPTURA DETENIDA' in text(terminal), timeout=15)
            screen = text(terminal)
            assert 'Qué puedes hacer' in screen, screen
            assert 'Esc menú' in screen and 'D detalles' in screen, screen
            assert diagnostic not in screen, screen
            terminal.send('D'); terminal.send('\x1b[F')
            assert diagnostic in text(terminal), text(terminal)
            terminal.send('d')
            if resource_error:
                assert 'No se pudo iniciar MPI con 2 procesos' in screen, screen
                assert 'Selecciona menos procesos' in screen, screen
            else:
                assert 'No se pudo completar la captura MPI' in screen, screen
                assert 'Selecciona menos procesos' not in screen, screen
            assert diagnostic not in text(terminal), text(terminal)
            for columns, rows in [(60, 24), (40, 18), (100, 35)]:
                resize(terminal, columns, rows)
                terminal.send('\x1b[H')
                screen = text(terminal)
                assert 'Esc menú' in screen, screen
                terminal.send('\x1b[F')
                assert 'detalles técnicos' in text(terminal), text(terminal)
            terminal.send('\x1b')
            wait(terminal, lambda: 'CAPTURA DETENIDA' not in text(terminal))
            terminal.send('r')
            terminal.send('\x1b[H' + '\x1b[3~' * 10 + '1\r')
            assert 'P:1' in text(terminal), text(terminal)
        finally:
            terminal.close()


if __name__ == '__main__':
    binary, root = map(lambda value: str(Path(value).resolve()), sys.argv[1:3])
    check(binary, root, 'processes to more cpus than are available in your allocation:', True)
    check(binary, root, 'Example worker initialization failure', False)
    print('MPI error messages: resource/generic failures, details, scrolling and return passed')

"""Line-based controls also commit asynchronous MPI replies without new input."""
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from test_ftxui_mpi import workers


def check(binary, root):
    process = subprocess.Popen([binary], cwd=root, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               env=dict(os.environ, OMP_NUM_THREADS='2'))
    data = ''
    observed = set()

    def send(command):
        process.stdin.write((command + '\n').encode()); process.stdin.flush()

    def wait(predicate):
        nonlocal data
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if select.select([process.stdout], [], [], .05)[0]:
                data += os.read(process.stdout.fileno(), 65536).decode(errors='replace')
            if predicate():
                return
        raise AssertionError(data[-5000:])

    def paused(generation):
        matches = re.findall(r'Generación: (\d+)', data)
        return matches and int(matches[-1]) == generation and data.rfind('Pausado') > data.rfind('Operación pendiente')

    try:
        send('C'); send('patterns/blinker.txt'); send('3'); send('M')
        wait(lambda: 'Motor seleccionado: MPI' in data)
        assert not workers(process.pid)
        send('2'); wait(lambda: len(workers(process.pid)) == 1 and paused(0))
        observed.update(workers(process.pid))
        send('n'); wait(lambda: paused(1))
        send('x'); wait(lambda: 'Visualizacion finalizada.' in data)
        send('2'); wait(lambda: paused(1))
        send('r'); wait(lambda: paused(0))
        send('q'); wait(lambda: not workers(process.pid) and
                       data.rfind('Visualizacion finalizada.') > data.rfind('Generación:'))
        send('0'); assert process.wait(timeout=5) == 0
        assert not any(Path(f'/proc/{pid}').exists() for pid in observed)
        print('PASS line-based MPI controls, asynchronous snapshots, menu/resume, reset and cleanup')
    finally:
        if process.poll() is None:
            send('q'); send('0')
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate(); process.wait(timeout=5)


if __name__ == '__main__':
    check(sys.argv[1], sys.argv[2])

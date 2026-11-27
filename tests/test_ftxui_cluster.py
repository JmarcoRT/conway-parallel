"""Two independent MPICH processes on loopback, including the unchanged CLI."""
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
from test_ftxui_didactic_mpi_views import cells, visual
from test_ftxui_responsive import resize

ESC = "\x1b"
LOOPBACK = "127.0.0.1"


def screen(terminal):
    return "\n".join(terminal.screen().values())


def visible(terminal, *needles):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if all(needle in screen(terminal) for needle in needles):
            return
        terminal.read(0.04)
    raise AssertionError((needles, screen(terminal)))


def read_plan(port):
    with urllib.request.urlopen(f"http://{LOOPBACK}:{port}/plan.json", timeout=1) as response:
        return json.load(response)


def free_port():
    with socket.socket() as listener:
        listener.bind((LOOPBACK, 0))
        return listener.getsockname()[1]


def bound_port(port):
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((LOOPBACK, port))
    listener.listen()
    return listener


def stop(process):
    if process and process.poll() is None:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=5)


def didactic(terminal):
    terminal.send("\r")
    visible(terminal, "Entrada · Visualización didáctica", "Tablero generado")
    terminal.send("\r")
    visible(terminal, "CATÁLOGO DE PATRONES")
    terminal.send("m")
    visible(terminal, "CARGAR PATRÓN")
    terminal.send("patterns/blinker.txt\r")
    terminal.send("u")
    visible(terminal, "DIDÁCTICA MPI · CLÚSTER", "Visible: G0 · Capturada: G1", "Ver reparto de filas")
    terminal.send(" ")
    visible(terminal, "Reproduciendo")
    terminal.send(" ")
    visible(terminal, "En pausa", "Paso 1 de 6")
    terminal.send("nv")
    visible(terminal, "Intercambiar bordes · 1/2", "Vista local", "H de R1 g3")
    assert cells(visual(terminal), "H de R1 g3") == "..O.."
    terminal.send("n")
    visible(terminal, "Intercambiar bordes · 2/2", "H borde fijo 0")
    terminal.send("n")
    visible(terminal, "Calcular una celda", "Vecinos vivos: 2")
    terminal.send("d")
    visible(terminal, "AYUDA MPI", "NW,N,NE,W,E,SW,S,SE")
    terminal.send("dn")
    visible(terminal, "Actualizar el tablero", "Nueva generación G1")
    terminal.send("n")
    visible(terminal, "Ver resultado", "Visible: G1 · Capturada: G1")
    terminal.send("]")
    visible(terminal, "Rank 1", "filas [3, 5)", "Rank o ventana sin captura")
    terminal.send("n  ")
    visible(terminal, "Visible: G1 · Capturada: G2", "Paso 1 de 6", "En pausa")
    terminal.send("nn")
    visible(terminal, "H de R0 g2")
    assert cells(visual(terminal), "H de R0 g2") == ".OOO."
    terminal.send("nnn")
    visible(terminal, "Ver resultado")
    terminal.send("nr")
    visible(terminal, "Visible: G0 · Capturada: G1", "Paso 1 de 6", "En pausa")
    terminal.send("nnnnn")
    visible(terminal, "Ver resultado")
    terminal.send("n" + ESC)
    visible(terminal, "Sesión del clúster", "Grupo listo · P=2")


def generated(terminal, directory):
    terminal.send("\r")
    visible(terminal, "Entrada · Visualización didáctica")
    terminal.send("\t")
    change = "\x1b[H" + "\x1b[3~" * 8 + "256\t"
    terminal.send(change + change + "\t\t\r")
    visible(terminal, "Tablero 256 × 256", "Visible: G0 · Capturada: G1", "Ventana filas")
    # The test owns TMPDIR. Retain the actual bytes emitted by the generator
    # so the independent full-board check uses precisely the same input.
    board, = directory.glob("conway-cluster-*/entrada.bin")
    source = board.read_bytes()
    assert source.startswith(b"256 256\n")
    terminal.send("\x1b[C")
    visible(terminal, "Ventana sin captura", "Tablero 256 × 256", "col. [1, 17)")
    terminal.send("nnnnnn")
    visible(terminal, "Visible: G1 · Capturada: G2", "col. [1, 17)")
    resize(terminal, 50, 24)
    visible(terminal, "DIDÁCTICA MPI · CLÚSTER", "Paso 1 de 6", "Tablero 256 × 256")
    terminal.assert_fits()
    terminal.send(ESC)
    visible(terminal, "Grupo listo · P=2")
    return source


def session(binary, repo, config, port):
    os.environ["CONWAY_CLUSTER_CONFIG"] = str(config)
    terminal = Terminal(str(binary), str(repo), 132, 42)
    client = None
    try:
        terminal.send("\t")
        visible(terminal, "> Clúster")
        terminal.send("\x1b[A")
        visible(terminal, "> Visualización didáctica")
        terminal.send("\x1b[B\r")
        visible(terminal, "Sesión del clúster", LOOPBACK, str(port), "Iniciar sesión")
        expected = (f"python3 scripts/cluster/cluster.py client --interactive --server-ip {LOOPBACK} "
                    f"--http-port {port} --node-id 1")
        visible(terminal, expected)

        with bound_port(port):
            terminal.send("\t\t\t\r")
            visible(terminal, "Error de sesión", "Publicando plan", "Iniciar sesión")

        terminal.send("\r")
        visible(terminal, "Conectando por MPI", "Plan disponible", "Falta cliente")
        assert "Grupo listo" not in screen(terminal)
        plan = read_plan(port)
        assert plan["interactive"] == 2 and plan["nodes"] == 2 and plan["clients"]["1"]["port_name"]

        terminal.send(ESC)
        visible(terminal, "Sesión cancelada", "Cierre de clientes no confirmado")
        with bound_port(port):
            pass

        terminal.send("\r")
        visible(terminal, "Plan disponible")
        client = subprocess.Popen(
            expected.split(), cwd=repo, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, start_new_session=True,
        )
        visible(terminal, "Grupo listo · P=2", "IP anunciada", socket.gethostname(), "Confirmado")
        resize(terminal, 40, 18)
        visible(terminal, "Grupo listo · P=2", "Nodo 0 · Servidor · Rank 0", "Equipo:")
        for _ in range(3):
            if "Nodo 1 · Cliente · Rank 1" in screen(terminal):
                break
            terminal.send("\x1b[6~")
        visible(terminal, "Nodo 1 · Cliente · Rank 1")
        for _ in range(2):
            if "Estado: Confirmado" in screen(terminal) and "IP: " + LOOPBACK in screen(terminal):
                break
            terminal.send("\x1b[B")
        visible(terminal, "IP: " + LOOPBACK, "Estado: Confirmado")
        terminal.assert_fits()
        assert client.poll() is None
        assert "SIMULACIÓN" not in screen(terminal) and "Generación" not in screen(terminal)
        resize(terminal, 132, 42)
        didactic(terminal)
        assert client.poll() is None
        source = generated(terminal, config.parent)
        assert client.poll() is None

        terminal.send(ESC)
        visible(terminal, "Sesión cerrada")
        output = client.communicate(timeout=5)[0]
        assert client.returncode == 0 and "Sesión cerrada" in output and "PASS" not in output, output
        with bound_port(port):
            pass
        terminal.send(ESC)
        terminal.send("q")
        assert terminal.process.wait(timeout=5) == 0
        return source
    finally:
        stop(client)
        terminal.close()


def full_board(repo, directory, port, source):
    # Reconstruct all of G1 with 256 independent 16×16 captures of the same
    # initial state. No request gives the UI the whole board.
    script = str(repo / "scripts/cluster/cluster.py")
    (directory / "entrada.bin").write_bytes(source)
    cells_before = source.split(b"\n", 1)[1]
    assert len(cells_before) == 256 * 256
    result = bytearray(len(cells_before))
    server = subprocess.Popen(
        ["python3", script, "ui-server", "--server-ip", LOOPBACK, "--http-port", str(port),
         "--nodes", "2", "--work-dir", str(directory)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, start_new_session=True, cwd=repo,
    )
    client = None
    pending = b""

    def receive(prefix):
        nonlocal pending
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                if line.startswith(prefix):
                    return line
                assert not line.startswith((b"DONE\tERROR", b"REJECT")), line
            if select.select([server.stdout], [], [], max(0, deadline - time.monotonic()))[0]:
                chunk = os.read(server.stdout.fileno(), 4096)
                assert chunk, pending
                pending += chunk
        raise AssertionError((prefix, pending))

    def send(command):
        server.stdin.write((command + "\n").encode())
        server.stdin.flush()

    try:
        receive(b"PLAN")
        client = subprocess.Popen(
            ["python3", script, "client", "--interactive", "--server-ip", LOOPBACK,
             "--http-port", str(port), "--node-id", "1"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, start_new_session=True, cwd=repo,
        )
        receive(b"READY")
        send("LOAD 1")
        assert receive(b"LOADED\t1\t").split(b"\t")[2:] == [b"256", b"256", str(sum(cells_before)).encode()]
        send("STEP 2 0 0 256 0")
        receive(b"REJECT\t2\t")
        request, peak, population = 3, 0, 0
        for y in range(0, 256, 16):
            for x in range(0, 256, 16):
                if request != 3:
                    send(f"RESET {request}")
                    receive(f"LOADED\t{request}\t".encode())
                    request += 1
                send(f"STEP {request} 0 {int(y >= 128)} {y} {x}")
                packet = receive(f"TRACE\t{request}\t".encode())
                fields = packet.split(b"\t")
                assert fields[4:10] == [str(v).encode() for v in (y, x, 256, 256, 16, 16)]
                before, after, lower, upper, _ = fields[12:]
                assert len(before) == len(after) == 256 and len(lower) <= 16 and len(upper) <= 16
                assert int(fields[10]) == sum(cells_before)
                if population:
                    assert int(fields[11]) == population
                population = int(fields[11])
                for row in range(16):
                    offset = (y + row) * 256 + x
                    segment = slice(row * 16, (row + 1) * 16)
                    assert before[segment] == bytes(value + 48 for value in cells_before[offset:offset + 16])
                    result[offset:offset + 16] = bytes(value - 48 for value in after[segment])
                request += 1
                peak = max(peak, len(packet) + 1)
        reference = bytearray(len(result))
        for y in range(256):
            for x in range(256):
                neighbors = sum(cells_before[r * 256 + c]
                                for r in range(max(0, y - 1), min(256, y + 2))
                                for c in range(max(0, x - 1), min(256, x + 2))
                                if (r, c) != (y, x))
                reference[y * 256 + x] = neighbors == 3 or (neighbors == 2 and cells_before[y * 256 + x])
        assert result == reference and sum(reference) == population and peak < 1024
        send("CLOSE")
        receive(b"DONE\tCLOSED")
        assert server.wait(timeout=5) == client.wait(timeout=5) == 0
        print(f"PASS 256×256: {len(result)} cells agree with B3/S23 reference; max TRACE = {peak} bytes")
    finally:
        stop(client)
        stop(server)


def legacy(repo, config, directory, port):
    pattern = repo / "patterns/glider.txt"
    data = {"role": "server", "server_ip": LOOPBACK, "http_port": port, "nodes": 2,
            "network_id": "loopback", "pattern": str(pattern), "steps": 20}
    config.write_text(json.dumps(data))
    client_config = directory / "client.json"
    client_config.write_text(json.dumps(dict(data, role="client", node_id=1)))
    command = ["python3", str(repo / "scripts/cluster/cluster.py"), "--skip-zerotier", "--config"]
    options = dict(cwd=repo, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT, text=True, start_new_session=True)
    server = subprocess.Popen([*command, str(config), "server"], **options)
    client = None
    try:
        client = subprocess.Popen([*command, str(client_config), "client"], **options)
        output = server.communicate(timeout=15)[0]
        remote = client.communicate(timeout=15)[0]
        assert client.returncode == server.returncode == 0, (output, remote)
        assert "PASS conway-cluster" in output and "P=2 steps=20 global_alive=4" in output, output
        metrics = (directory / "runtime/metrics.csv").read_text().splitlines()
        assert len(metrics) == 3 and metrics[0] == "rank,first_row,rows,alive,seconds", metrics
        board = (directory / "runtime/board_final.txt").read_text().splitlines()
        assert board[0] == "6 7" and sum(row.count("1") for row in board[1:]) == 4, board
    finally:
        stop(client)
        stop(server)


if __name__ == "__main__":
    conway, cluster, repo = (Path(arg).resolve() for arg in sys.argv[1:])
    os.environ["CONWAY_CLUSTER_BIN"] = str(cluster)
    with tempfile.TemporaryDirectory() as work:
        directory = Path(work)
        config = directory / "server.json"
        port = free_port()
        config.write_text(json.dumps({"server_ip": LOOPBACK, "http_port": port, "nodes": 2}))
        os.environ["TMPDIR"] = str(directory)
        source = session(conway, repo, config, port)
        print("PASS FTXUI: session, six stages, real halos, reset, reentry and collective close", flush=True)
        full_board(repo, directory, port, source)
        legacy(repo, config, directory, port)
        print("PASS CLI: glider, P=2, 20 generations, 4 live cells and both output files")

"""Two independent MPICH processes on loopback, including the unchanged CLI."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

sys.dont_write_bytecode = True
from test_ftxui_header import Terminal
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
        assert plan["interactive"] and plan["nodes"] == 2 and plan["clients"]["1"]["port_name"]

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
        terminal.send("\x1b[B")
        visible(terminal, "IP: " + LOOPBACK, "Estado: Confirmado")
        terminal.assert_fits()
        assert client.poll() is None
        assert "SIMULACIÓN" not in screen(terminal) and "Generación" not in screen(terminal)

        terminal.send(ESC)
        visible(terminal, "Sesión cerrada")
        output = client.communicate(timeout=5)[0]
        assert client.returncode == 0 and "Sesión cerrada" in output and "PASS" not in output, output
        with bound_port(port):
            pass
        terminal.send(ESC)
        terminal.send("q")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop(client)
        terminal.close()


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
        session(conway, repo, config, port)
        print("PASS FTXUI: retry, cancellation, connected group, narrow member view and confirmed close")
        legacy(repo, config, directory, port)
        print("PASS CLI: glider, P=2, 20 generations, 4 live cells and both output files")

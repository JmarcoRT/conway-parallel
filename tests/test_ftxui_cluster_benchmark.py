"""Persistent MPI benchmark through FTXUI, including group reuse and loss."""
import csv
import json
import math
import os
from pathlib import Path
import signal
from statistics import median
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from test_ftxui_cluster import ESC, LOOPBACK, Terminal, free_port, read_plan, screen, stop, visible


def read_csv(directory, name):
    with (directory / (name + ".csv")).open(newline="") as file:
        return list(csv.DictReader(file))


def verify_export(directory, generations, repetitions, warmups):
    samples = read_csv(directory, "samples")
    summary = {row["engine"]: row for row in read_csv(directory, "summary")}
    metadata = {row["key"]: row["value"] for row in read_csv(directory, "metadata")}
    assert len(samples) == 2 * repetitions
    assert set(summary) == {"sequential", "mpi"}
    for row in samples:
        assert row["valid"] == row["equivalent"] == "true", row
        assert int(row["generations"]) == generations, row
        if row["engine"] == "mpi":
            assert row["configuration_id"] == "mpi-distributed:2", row
            assert row["timing_scope"] == "mpi_distributed_rank_max_batch", row
            assert row["observed_processes"] == row["requested_processes"] == "2", row
            assert not row["session_index"], row
            assert all(float(row[key]) >= 0 for key in ("load_ms", "snapshot_ms", "batch_request_ms")), row
            assert int(row["warmups_in_session"]) == warmups, row
    for engine, record in summary.items():
        series = [float(row["elapsed_ms"]) for row in samples if row["engine"] == engine]
        assert float(record["median_ms"]) == median(series), record
        assert record["equivalent"] == record["valid"] == "true", record
    assert math.isclose(float(summary["mpi"]["speedup"]),
                        float(summary["sequential"]["median_ms"]) / float(summary["mpi"]["median_ms"]))
    assert metadata["mpi.topology"] == "distributed" and metadata["mpi.processes"] == "2"
    assert metadata["mpi.rank.0.role"] == "server" and metadata["mpi.rank.1.role"] == "client"
    assert metadata["mpi.rank.0.announced_ip"] == metadata["mpi.rank.1.announced_ip"] == LOOPBACK
    assert all(metadata[f"mpi.rank.{rank}.hostname"] for rank in (0, 1))
    assert int(metadata["execution.completed_warmup_runs"]) == 2 * warmups
    assert "reused" in metadata["mpi.lifecycle"]


def wait_for_request(directory, previous_stamp=0, cells=1000000):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        for file in directory.glob("conway-cluster-*/entrada.bin"):
            stat = file.stat()
            if stat.st_size >= cells and stat.st_mtime_ns > previous_stamp:
                return stat.st_mtime_ns
        time.sleep(0.03)
    raise AssertionError("Benchmark did not submit the initial board")


def run(conway, cluster, repo, directory):
    config = directory / "server.json"
    trace = directory / "transfer.log"
    port = free_port()
    config.write_text(json.dumps({"server_ip": LOOPBACK, "http_port": port, "nodes": 2}))
    os.environ.update(CONWAY_CLUSTER_CONFIG=str(config), CONWAY_CLUSTER_BIN=str(cluster),
                      TMPDIR=str(directory), CONWAY_CLUSTER_TRACE_TRANSFER="1",
                      CONWAY_CLUSTER_DIAGNOSTICS=str(trace))
    terminal = Terminal(str(conway), str(directory), 132, 46)
    client = None
    try:
        terminal.send("\t\r")
        visible(terminal, "Iniciar sesión")
        assert "[ Benchmark ]" not in screen(terminal)
        terminal.send("\t\t\t\r")
        visible(terminal, "Plan disponible")
        plan = read_plan(port)
        assert plan["interactive"] == 4
        with (directory / "client.log").open("w") as log:
            client = subprocess.Popen(
                ["python3", str(repo / "scripts/cluster/cluster.py"), "client", "--interactive",
                 "--server-ip", LOOPBACK, "--http-port", str(port), "--node-id", "1",
                 "--binary", str(cluster)], cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        visible(terminal, "Grupo listo · P=2", "[ Benchmark ]")
        children = Path(f"/proc/{client.pid}/task/{client.pid}/children")
        rank, = map(int, children.read_text().split())
        terminal.send("\t\r")
        visible(terminal, "BENCHMARK DEL CLÚSTER", "Secuencial en la PC", "MPI distribuido · P=2", "Tablero generado")
        # Three generations make a mistaken continuation differ from the reference.
        terminal.send("\t" * 6 + "\x1b[H" + "\x1b[3~" * 2 + "3" + "\t" * 4)
        os.kill(rank, signal.SIGSTOP)
        terminal.send("\r")
        visible(terminal, "Ejecutando benchmark", "1/12 intentos")
        stamp = wait_for_request(directory)
        os.kill(rank, signal.SIGCONT)
        visible(terminal, "Comparación completada", "Todos los motores producen el mismo resultado",
                "MPI distribuido", "1000 × 1000")
        terminal.send("\x1b[C")
        visible(terminal, "MEDICIONES INDIVIDUALES", "MPI distribuido", "Correcto")
        terminal.send("\x1b[C")
        visible(terminal, "MPI distribuido reutiliza el grupo confirmado", "P=2", "MPI_Wtime")
        terminal.send("e")
        visible(terminal, "CSV guardados en")
        exported, = directory.glob("benchmark-cluster-*")
        verify_export(exported, 3, 5, 1)
        assert trace.read_text().count("[reparto fin]") == 6
        print("PASS 1000×1000: 12 intentos, seis repartos G0, equivalencia completa, pestañas y tres CSV", flush=True)

        terminal.send(ESC)
        visible(terminal, "Grupo listo · P=2")
        assert read_plan(port)["session_id"] == plan["session_id"] and client.poll() is None
        terminal.send("\r")
        visible(terminal, "BENCHMARK DEL CLÚSTER", "Tablero generado")
        # Pause this attempt and request exit, then let the same group finish it.
        os.kill(rank, signal.SIGSTOP)
        terminal.send("\r")
        visible(terminal, "1/12 intentos")
        stamp = wait_for_request(directory, stamp)
        terminal.send(ESC)
        visible(terminal, "Esperando el intento en curso")
        os.kill(rank, signal.SIGCONT)
        visible(terminal, "Grupo listo · P=2")
        assert trace.read_text().count("[reparto fin]") == 7
        assert read_plan(port)["session_id"] == plan["session_id"] and client.poll() is None
        terminal.send("\r")
        visible(terminal, "BENCHMARK DEL CLÚSTER")
        terminal.send("\r")
        visible(terminal, "Comparación completada", "Todos los motores producen el mismo resultado")
        terminal.send(ESC)
        visible(terminal, "Grupo listo · P=2")
        assert trace.read_text().count("[reparto fin]") == 13
        print("PASS Esc drena un intento, vuelve a Grupo listo y permite medir otra vez", flush=True)

        terminal.send("\r")
        visible(terminal, "BENCHMARK DEL CLÚSTER")
        terminal.send("\t\t\r")
        visible(terminal, "CATÁLOGO DE PATRONES")
        terminal.send("m")
        visible(terminal, "CARGAR PATRÓN")
        terminal.send(str(repo / "patterns/blinker.txt") + "\r")
        terminal.send("u")
        visible(terminal, "BENCHMARK DEL CLÚSTER", "blinker.txt")
        terminal.send("\t" * 5 + "\r")
        visible(terminal, "Comparación completada", "blinker.txt de 5 × 5", "Todos los motores producen el mismo resultado")
        terminal.send("e")
        visible(terminal, "CSV guardados en")
        pattern_export, = set(directory.glob("benchmark-cluster-*")) - {exported}
        verify_export(pattern_export, 3, 5, 1)
        stamp = wait_for_request(directory, stamp, cells=25)
        terminal.send(ESC)
        visible(terminal, "Grupo listo · P=2")
        assert read_plan(port)["session_id"] == plan["session_id"]
        print("PASS patrón por ruta manual en el mismo grupo", flush=True)

        terminal.send("\r")
        visible(terminal, "BENCHMARK DEL CLÚSTER")
        os.kill(rank, signal.SIGSTOP)
        terminal.send("\r")
        visible(terminal, "1/12 intentos")
        wait_for_request(directory, stamp, cells=25)
        stop(client)
        visible(terminal, "Estado: Error", "Grupo MPI perdido durante Benchmark", timeout=17)
        terminal.send(ESC)
        visible(terminal, "Error de sesión", "Cierre de clientes no confirmado")
        terminal.send(ESC)
        terminal.send("q")
        assert terminal.process.wait(timeout=5) == 0
        print("PASS pérdida del cliente durante Benchmark visible en la UI", flush=True)
    finally:
        stop(client)
        try:
            terminal.close()
        except subprocess.TimeoutExpired:
            terminal.process.kill()
            terminal.close()


if __name__ == "__main__":
    conway, cluster, repo = (Path(arg).resolve() for arg in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="conway-cluster-benchmark-") as work:
        run(conway, cluster, repo, Path(work))

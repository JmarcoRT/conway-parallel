#!/usr/bin/env python3
"""Bootstrap minimo del cluster de Conway Parallel.

Python prepara la red y lanza los procesos; el binario C++ forma el
MPI_Comm. No usa SSH ni mpirun entre nodos.

Uso:
    python3 cluster.py init
    python3 cluster.py doctor
    python3 cluster.py server
    python3 cluster.py client

Prueba local sin ZeroTier:
    python3 cluster.py --skip-zerotier --config /tmp/n0.json init ...
"""

import argparse
import getpass
import ipaddress
import json
import os
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

DEFAULT_CONFIG = Path.home() / ".config" / "conway" / "cluster.json"
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_BINARY = Path(
    os.environ.get(
        "CONWAY_CLUSTER_BIN",
        str(SCRIPT_DIR.parent.parent / "build-mpich" / "conway_cluster"),
    )
)
ZT_API = os.environ.get("CONWAY_ZT_API", "https://my.zerotier.com/api/v1")
DEFAULT_HTTP_PORT = 8080
DEFAULT_TIMEOUT = 300


class ClusterError(Exception):
    def __init__(self, message: str, action: str = "") -> None:
        super().__init__(message)
        self.message = message
        self.action = action


def load_config(path: Path) -> dict:
    if not path.exists():
        raise ClusterError(
            f"No existe la configuracion {path}.",
            "Ejecuta `python3 cluster.py init` una vez en este nodo.",
        )
    with path.open() as f:
        return json.load(f)


def save_config(path: Path, config: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(config, indent=2) + "\n")
    path.chmod(0o600)


def ask(label: str, default: str | None = None) -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{label}{suffix}: ").strip()
    return value or (default or "")


def init_config(args) -> dict:
    role = args.role
    if not role:
        if not sys.stdin.isatty():
            raise ClusterError(
                "Falta --role en modo no interactivo.",
                "Usa --role server o --role client.",
            )
        print("Rol de este nodo:")
        print("  1) Servidor")
        print("  2) Cliente")
        role = {"1": "server", "2": "client"}.get(input("Elige 1 o 2: ").strip(), "")

    if role not in ("server", "client"):
        raise ClusterError("Rol no valido.", "Elige server o client.")

    network_id = args.network_id or ask("Network ID de ZeroTier")
    if not network_id:
        raise ClusterError("Falta el network ID.", "Consiguelo en el panel de ZeroTier.")

    nodes = args.nodes
    if nodes is None:
        nodes = int(ask("Numero total de nodos", "4"))

    server_ip = args.server_ip or ask("IP del servidor en ZeroTier", "10.147.20.1")
    http_port = args.http_port or DEFAULT_HTTP_PORT
    default_pattern = str(REPO_ROOT / "patterns" / "glider.txt")
    pattern = args.pattern
    if not pattern:
        pattern = (
            ask("Patron de Conway", default_pattern)
            if sys.stdin.isatty()
            else default_pattern
        )
    steps = args.steps
    if steps is None:
        steps = int(ask("Pasos a simular", "30")) if sys.stdin.isatty() else 30

    config = {
        "role": role,
        "network_id": network_id,
        "node_id": 0,
        "nodes": nodes,
        "server_ip": server_ip,
        "http_port": http_port,
        "pattern": pattern,
        "steps": steps,
    }

    if role == "server":
        token = args.api_token
        if not token and sys.stdin.isatty():
            token = getpass.getpass(
                "API token de ZeroTier (opcional; Enter para autorizacion manual): "
            ).strip()
        if token:
            config["api_token"] = token
    else:
        node_id = args.node_id
        if node_id is None:
            node_id = int(ask("Numero de este nodo (1..N-1)", "1"))
        if not 1 <= node_id < nodes:
            raise ClusterError(
                f"El nodo debe estar entre 1 y {nodes - 1}.",
                "Revisa el numero de nodo.",
            )
        config["node_id"] = node_id

    return config


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, text=True, **kwargs)


def zerotier_cli() -> str:
    path = shutil.which("zerotier-cli")
    if not path:
        raise ClusterError(
            "ZeroTier no esta instalado.",
            "Ejecuta: curl -s https://install.zerotier.com | sudo bash",
        )
    return path


def zt_ip() -> str | None:
    result = run(["ip", "-o", "-4", "addr", "show"], capture_output=True)
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) < 4 or not parts[1].startswith("zt"):
            continue
        if "inet" in parts:
            return parts[parts.index("inet") + 1].split("/")[0]
    return None


def wait_zt_ip(timeout: int = 120) -> str:
    for _ in range(timeout):
        ip = zt_ip()
        if ip:
            return ip
        time.sleep(1)
    raise ClusterError(
        "ZeroTier no tiene IP en este nodo.",
        "El nodo aun no esta autorizado. Ejecuta el servidor para autorizarlo "
        "o autorizalo en el panel de ZeroTier.",
    )


def join_network(network_id: str, skip: bool) -> None:
    if skip:
        return
    zerotier_cli()
    result = run(["sudo", "zerotier-cli", "join", network_id], capture_output=True)
    if result.returncode != 0 and "already" not in (result.stdout + result.stderr).lower():
        raise ClusterError(
            f"No se pudo unir a la red {network_id}.",
            "Revisa que ZeroTier este instalado y que tengas permisos sudo.",
        )


def zt_members(network_id: str, token: str) -> list[dict]:
    request = urllib.request.Request(
        f"{ZT_API}/network/{network_id}/member",
        headers={"Authorization": f"Bearer {token}"},
    )
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.load(response)


def authorize_pending(config: dict, timeout: int = 30) -> None:
    """Autoriza a los miembros pendientes, incluido el propio servidor.

    Se ejecuta antes de esperar la IP local para evitar el bloqueo cuando la
    red es privada y el nodo servidor todavia no esta autorizado.
    """
    token = config.get("api_token", "")
    if not token:
        print("AVISO: no hay API token; autoriza este nodo en el panel de ZeroTier.")
        return

    network_id = config["network_id"]
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            members = zt_members(network_id, token)
        except (urllib.error.HTTPError, urllib.error.URLError, OSError) as exc:
            print(f"AVISO: no se pudo consultar ZeroTier: {exc}")
            time.sleep(2)
            continue

        pending = [
            m for m in members if not m.get("config", {}).get("authorized", False)
        ]
        if not pending:
            return
        for member in pending:
            member_id = member["id"]
            request = urllib.request.Request(
                f"{ZT_API}/network/{network_id}/member/{member_id}",
                data=json.dumps({"config": {"authorized": True}}).encode(),
                headers={
                    "Authorization": f"Bearer {token}",
                    "Content-Type": "application/json",
                },
                method="POST",
            )
            urllib.request.urlopen(request, timeout=15).read()
            print(f"Autorizando nodo {member_id}...")
        return

    print("AVISO: no se pudo confirmar la autorizacion del nodo servidor.")


def authorize_members(config: dict, timeout: int = 300) -> None:
    token = config.get("api_token", "")
    if not token:
        print("AVISO: no hay API token; autoriza los nodos en el panel de ZeroTier.")
        return

    network_id = config["network_id"]
    expected = int(config["nodes"])
    deadline = time.time() + timeout

    while time.time() < deadline:
        try:
            members = zt_members(network_id, token)
        except urllib.error.HTTPError as exc:
            if exc.code in (401, 403):
                raise ClusterError(
                    "El API token de ZeroTier no es valido o expiro.",
                    "Genera uno nuevo en https://my.zerotier.com/account "
                    "y vuelve a ejecutar init.",
                )
            raise ClusterError(
                f"ZeroTier respondio HTTP {exc.code}.",
                "Revisa el network ID y el estado del servicio.",
            )
        except OSError as exc:
            raise ClusterError(
                f"No se pudo consultar ZeroTier: {exc}.",
                "Revisa la conexion a internet.",
            )

        for member in members:
            if not member.get("config", {}).get("authorized", False):
                member_id = member["id"]
                request = urllib.request.Request(
                    f"{ZT_API}/network/{network_id}/member/{member_id}",
                    data=json.dumps({"config": {"authorized": True}}).encode(),
                    headers={
                        "Authorization": f"Bearer {token}",
                        "Content-Type": "application/json",
                    },
                    method="POST",
                )
                urllib.request.urlopen(request, timeout=15).read()
                print(f"Autorizando nodo {member_id}...")

        authorized = sum(
            1 for m in members if m.get("config", {}).get("authorized", False)
        )
        if authorized >= expected:
            print(f"Nodos autorizados: {authorized}/{expected}")
            return
        time.sleep(2)

    raise ClusterError(
        f"Timeout esperando {expected} nodos autorizados.",
        "Revisa que los clientes hayan ejecutado `join` y vuelve a intentar.",
    )


class PlanHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, directory: str, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def log_message(self, *args):
        pass


def start_plan_server(config: dict, runtime_dir: Path) -> ThreadingHTTPServer:
    runtime_dir.mkdir(parents=True, exist_ok=True)
    handler = lambda *a, **kw: PlanHandler(  # noqa: E731
        *a, directory=str(runtime_dir), **kw
    )
    server = ThreadingHTTPServer(
        (config["server_ip"], int(config["http_port"])), handler
    )
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def plan_url(config: dict) -> str:
    return f"http://{config['server_ip']}:{config['http_port']}/plan.json"


def wait_plan(config: dict, timeout: int = 300, *, direct: bool = False) -> dict:
    deadline = time.monotonic() + timeout
    last_error = None
    open_url = (urllib.request.build_opener(urllib.request.ProxyHandler({})).open
                if direct else urllib.request.urlopen)
    while time.monotonic() < deadline:
        try:
            with open_url(plan_url(config), timeout=3) as response:
                plan = json.load(response)
                if not isinstance(plan, dict):
                    raise ValueError("El plan debe ser un objeto JSON.")
                return plan
        except (OSError, ValueError) as exc:
            last_error = exc
        time.sleep(2)
    raise ClusterError(
        f"No se pudo leer el plan del servidor ({last_error}).",
        ("Comprueba que la PC muestre «Plan disponible» y que ambos nodos estén en la misma red."
         if direct else "Verifica que `python3 cluster.py server` este ejecutandose "
                        "y que ambos nodos esten en la misma red."),
    )


def ensure_binary(binary: Path) -> Path:
    if binary.exists():
        return binary
    raise ClusterError(
        f"No existe el binario del cluster: {binary}",
        "Compila con: cmake --build build-mpich --target conway_cluster",
    )


def run_binary(binary: Path, args: list[str]) -> int:
    try:
        return subprocess.run(
            [str(binary)] + args, timeout=DEFAULT_TIMEOUT
        ).returncode
    except subprocess.TimeoutExpired as exc:
        raise ClusterError(
            f"El cluster no termino en {DEFAULT_TIMEOUT} segundos.",
            "Revisa que todos los nodos esten autorizados y que el plan HTTP sea alcanzable.",
        ) from exc


def cmd_init(args) -> int:
    config = init_config(args)
    save_config(args.config, config)

    print(f"Configuracion guardada en {args.config}")
    print(f"Rol: {config['role']}")
    print(f"Red: {config['network_id']}")
    print(f"Nodos: {config['nodes']}")
    if config["role"] == "client":
        print(f"Nodo: {config['node_id']}")
    print("Siguiente paso: python3 cluster.py doctor")
    return 0


def cmd_doctor(args) -> int:
    config = load_config(args.config)
    errors = 0

    if args.skip_zerotier:
        print("[OK   ] ZeroTier: omitido")
    else:
        try:
            zerotier_cli()
            ip = zt_ip()
            if ip:
                print(f"[OK   ] ZeroTier: {ip}")
            else:
                print("[ERROR] ZeroTier: sin IP; falta autorizacion")
                errors += 1
        except ClusterError as exc:
            print(f"[ERROR] ZeroTier: {exc.message}")
            errors += 1

    compiler = os.environ.get("MPICXX", "mpicxx")
    if not shutil.which(compiler):
        print(f"[ERROR] MPICH: no se encontro {compiler}")
        errors += 1
    else:
        shown = run([compiler, "-show"], capture_output=True).stdout.lower()
        is_mpich = "mpich" in shown or "mpich" in compiler.lower()
        if is_mpich:
            print("[OK   ] MPICH")
        else:
            print("[ERROR] mpicxx no apunta a MPICH")
            errors += 1

    if shutil.which("nvidia-smi"):
        print("[OK   ] CUDA: nvidia-smi disponible")
    else:
        print("[AVISO] CUDA: no se encontro nvidia-smi")

    if errors:
        print("\nRevisa los ERROR antes de iniciar el cluster.")
        return 1

    print("\nTodo listo.")
    return 0


def cmd_server(args) -> int:
    config = load_config(args.config)
    if config["role"] != "server":
        raise ClusterError(
            "Este nodo esta configurado como cliente.",
            "Ejecuta `python3 cluster.py client` o reconfigura con init.",
        )

    join_network(config["network_id"], args.skip_zerotier)
    if not args.skip_zerotier:
        # El servidor tambien necesita estar autorizado antes de esperar su IP.
        authorize_pending(config)
        ip = wait_zt_ip()
        print(f"ZeroTier listo: {ip}")
        authorize_members(config)

    runtime_dir = args.config.parent / "runtime"
    plan_path = runtime_dir / "plan.json"
    plan_path.unlink(missing_ok=True)

    server = start_plan_server(config, runtime_dir)
    print(f"Plan publicado en {plan_url(config)}")

    binary = ensure_binary(args.binary)
    try:
        return run_binary(
            binary,
            [
                "--server",
                "--clients",
                str(int(config["nodes"]) - 1),
                "--plan",
                str(plan_path),
                "--pattern",
                str(config["pattern"]),
                "--steps",
                str(config["steps"]),
                "--metrics",
                str(runtime_dir / "metrics.csv"),
                "--board",
                str(runtime_dir / "board_final.txt"),
            ],
        )
    finally:
        server.shutdown()
        server.server_close()


def cmd_client(args) -> int:
    if args.interactive:
        return interactive_client(args)
    config = load_config(args.config)
    if config["role"] != "client":
        raise ClusterError(
            "Este nodo esta configurado como servidor.",
            "Ejecuta `python3 cluster.py server` o reconfigura con init.",
        )

    join_network(config["network_id"], args.skip_zerotier)
    if not args.skip_zerotier:
        ip = wait_zt_ip()
        print(f"ZeroTier listo: {ip}")

    plan = wait_plan(config)
    if plan.get("interactive") is True:
        raise ClusterError("Usa el comando interactivo que muestra la sesión del clúster.")
    node_id = str(config["node_id"])
    try:
        port_name = plan["clients"][node_id]["port_name"]
    except KeyError:
        raise ClusterError(
            f"El plan no contiene el nodo {node_id}.",
            "Revisa el numero de nodo o reinicia el servidor.",
        )

    binary = ensure_binary(args.binary)
    return run_binary(
        binary,
        [
            "--client",
            "--id",
            node_id,
            "--clients",
            str(int(config["nodes"]) - 1),
            "--port-name",
            port_name,
            "--pattern",
            str(config["pattern"]),
            "--steps",
            str(config["steps"]),
        ],
    )


def ui_message(*fields) -> None:
    print("\t".join("".join(c if c.isprintable() else " " for c in str(field))
                    for field in fields), flush=True)


def ui_defaults(args) -> int:
    try:
        config = load_config(args.config) if args.config.exists() else {}
        if not isinstance(config, dict):
            raise ValueError("se esperaba un objeto JSON")
        ui_message("DEFAULTS", config.get("server_ip") or zt_ip() or "",
                   config.get("http_port", DEFAULT_HTTP_PORT), config.get("nodes", 2))
        return 0
    except (OSError, ValueError) as exc:
        ui_message("DONE", "ERROR", f"No se pudo leer la configuración inicial: {exc}")
        return 2


def announced_ip(destination: str) -> str:
    if ipaddress.IPv4Address(destination).is_loopback:
        return destination
    try:
        result = run(["ip", "-o", "-4", "route", "get", destination], capture_output=True)
    except OSError:
        return ""
    tokens = result.stdout.split()
    fields = dict(zip(tokens, tokens[1:]))
    if fields.get("dev", "").startswith("zt"):
        return fields.get("src", "")
    return ""


def interactive_client(args) -> int:
    if args.server_ip is None or args.node_id is None or args.node_id < 1 or not 1 <= args.http_port <= 65535:
        raise ClusterError("Indica --server-ip, --http-port y --node-id para la sesión interactiva.")
    try:
        address = str(ipaddress.IPv4Address(args.server_ip))
    except ValueError as exc:
        raise ClusterError("La IP del servidor debe ser una IPv4.") from exc
    config = {
        "server_ip": address,
        "http_port": args.http_port,
    }
    plan = wait_plan(config, timeout=DEFAULT_TIMEOUT, direct=True)
    if plan.get("interactive") is not True:
        raise ClusterError("El servidor no ofrece una sesión interactiva.")
    try:
        port_name = plan["clients"][str(args.node_id)]["port_name"]
    except KeyError as exc:
        raise ClusterError(f"El plan no contiene el nodo {args.node_id}.") from exc
    binary = ensure_binary(args.binary)
    # The join has a deadline, but a confirmed group may remain idle.
    with subprocess.Popen(
        [str(binary), "--client", "--interactive", "--id", str(args.node_id),
         "--clients", str(int(plan["nodes"]) - 1), "--port-name", port_name,
         "--hostname", socket.gethostname(), "--address", announced_ip(config["server_ip"])],
        stdout=subprocess.PIPE, stdin=subprocess.DEVNULL, bufsize=0,
    ) as child:
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(child.stdout, selectors.EVENT_READ)
                deadline = time.monotonic() + DEFAULT_TIMEOUT
                while deadline is not None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not selector.select(remaining):
                        raise ClusterError("Plazo agotado conectando por MPI.")
                    line = child.stdout.readline()
                    if not line:
                        return child.wait()
                    sys.stdout.buffer.write(line)
                    sys.stdout.buffer.flush()
                    if line.startswith(b"Grupo listo:"):
                        deadline = None
            shutil.copyfileobj(child.stdout, sys.stdout.buffer)
            sys.stdout.buffer.flush()
            return child.wait()
        finally:
            stop_child(child)


class InteractivePlanHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/plan.json":
            self.send_error(404)
            return
        try:
            plan = self.server.plan_path.read_bytes()
        except FileNotFoundError:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(plan)))
        self.end_headers()
        self.wfile.write(plan)

    def log_message(self, *args):
        pass


def stop_child(child):
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=1)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()


def relay_session(child, url, diagnostics, nodes):
    phase = "Publicando plan"
    ready = closing = closed = False
    deadline = time.monotonic() + DEFAULT_TIMEOUT
    pending = b""
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with selectors.DefaultSelector() as selector:
        selector.register(sys.stdin.buffer, selectors.EVENT_READ, "ui")
        selector.register(child.stdout, selectors.EVENT_READ, "mpi")
        while True:
            if deadline is not None and time.monotonic() >= deadline:
                raise ClusterError(f"{phase}: plazo agotado.")
            for key, _ in selector.select(0.1):
                if key.data == "ui":
                    # The UI sends only one command. EOF also cancels the session.
                    os.read(key.fd, 4096)
                    selector.unregister(key.fileobj)
                    if not ready:
                        return "CANCELLED", f"{phase}: se cerró el servidor local. Cierre de clientes no confirmado."
                    closing = True
                    phase = "Cerrando sesión"
                    deadline = time.monotonic() + 5
                    try:
                        child.stdin.write(b"CLOSE\n")
                        child.stdin.flush()
                    except OSError as exc:
                        raise ClusterError(f"{phase}: se perdió el canal MPI.") from exc
                    continue

                data = os.read(key.fd, 4096)
                if not data:
                    code = child.wait(timeout=1)
                    if closing and closed and code == 0:
                        return "CLOSED", "Cierre confirmado por el grupo MPI."
                    diagnostics.seek(max(0, diagnostics.tell() - 2048))
                    detail = diagnostics.read().decode(errors="replace").strip()
                    raise ClusterError(f"{phase}: el proceso MPI terminó ({code}). {detail}")
                pending += data
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    decoded = line.decode(errors="replace")
                    if decoded == "PORTS":
                        try:
                            with opener.open(url, timeout=1) as response:
                                plan = json.load(response)
                        except (OSError, ValueError) as exc:
                            raise ClusterError(f"{phase}: no se pudo leer el plan HTTP. {exc}") from exc
                        if (plan.get("interactive") is not True or plan["nodes"] != nodes or
                            set(plan["clients"]) != {str(i) for i in range(1, nodes)} or
                            any(not item["port_name"] for item in plan["clients"].values())):
                            raise ClusterError("Publicando plan: el plan está incompleto.")
                        phase = "Conectando por MPI"
                        deadline = time.monotonic() + DEFAULT_TIMEOUT
                        ui_message("PLAN")
                    elif decoded == "READY":
                        phase, ready, deadline = "Grupo listo", True, None
                        ui_message("READY")
                    elif decoded == "CLOSED":
                        closed = True
                    elif decoded.startswith(("JOIN\t", "MEMBER\t")):
                        ui_message(*decoded.split("\t"))
                if len(pending) > 8192:
                    raise ClusterError(f"{phase}: respuesta MPI demasiado larga.")


def ui_server(args) -> int:
    launched = False
    status, detail = "ERROR", ""
    try:
        ip = str(ipaddress.IPv4Address(args.server_ip))
        if ip == "0.0.0.0" or not 1 <= args.http_port <= 65535 or not 2 <= args.nodes <= 1024:
            raise ClusterError("Publicando plan: comprueba la IP, el puerto y el total de nodos.")
        binary = ensure_binary(args.binary)
        plan = args.work_dir / "plan.json"
        with ThreadingHTTPServer((ip, args.http_port), InteractivePlanHandler) as server:
            server.plan_path = plan
            http = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.1})
            http.start()
            try:
                with tempfile.TemporaryFile() as diagnostics, subprocess.Popen(
                    [str(binary), "--server", "--interactive", "--clients", str(args.nodes - 1),
                     "--plan", str(plan), "--hostname", socket.gethostname(), "--address", ip],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=diagnostics,
                ) as child:
                    launched = True
                    try:
                        url = plan_url({"server_ip": ip, "http_port": args.http_port})
                        status, detail = relay_session(child, url, diagnostics, args.nodes)
                    finally:
                        stop_child(child)
            finally:
                server.shutdown()
                http.join()
    except ClusterError as exc:
        detail = exc.message
        if not detail.startswith(("Publicando plan:", "Conectando por MPI:", "Grupo listo:", "Cerrando sesión:")):
            detail = "Publicando plan: " + detail
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as exc:
        phase = "Sesión MPI" if launched else "Publicando plan"
        detail = f"{phase}: {exc}."
    if status == "ERROR" and launched:
        detail += " Cierre de clientes no confirmado."
    ui_message("DONE", status, detail)
    return 0 if status != "ERROR" else 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Cluster minimo de Conway Parallel")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--skip-zerotier", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)

    init = sub.add_parser("init", help="Configura este nodo una vez")
    init.add_argument("--role", choices=["server", "client"])
    init.add_argument("--network-id")
    init.add_argument("--api-token")
    init.add_argument("--node-id", type=int)
    init.add_argument("--nodes", type=int)
    init.add_argument("--server-ip")
    init.add_argument("--http-port", type=int)
    init.add_argument("--pattern")
    init.add_argument("--steps", type=int)
    init.set_defaults(func=cmd_init)

    doctor = sub.add_parser("doctor", help="Verifica los requisitos")
    doctor.set_defaults(func=cmd_doctor)

    server = sub.add_parser("server", help="Inicia el servidor del cluster")
    server.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    server.set_defaults(func=cmd_server)

    client = sub.add_parser("client", help="Se une como cliente")
    client.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    client.add_argument("--interactive", action="store_true")
    client.add_argument("--server-ip")
    client.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT)
    client.add_argument("--node-id", type=int)
    client.set_defaults(func=cmd_client)

    defaults = sub.add_parser("ui-defaults")
    defaults.set_defaults(func=ui_defaults)
    session = sub.add_parser("ui-server")
    session.add_argument("--server-ip", required=True)
    session.add_argument("--http-port", type=int, required=True)
    session.add_argument("--nodes", type=int, required=True)
    session.add_argument("--work-dir", type=Path, required=True)
    session.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    session.set_defaults(func=ui_server)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except ClusterError as exc:
        print(f"ERROR: {exc.message}", file=sys.stderr)
        if exc.action:
            print(f"Que hacer: {exc.action}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nCancelado.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())

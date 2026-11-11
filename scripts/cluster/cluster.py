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
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
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


def wait_plan(config: dict, timeout: int = 300) -> dict:
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(plan_url(config), timeout=3) as response:
                return json.load(response)
        except (urllib.error.URLError, urllib.error.HTTPError, OSError) as exc:
            last_error = exc
        time.sleep(2)
    raise ClusterError(
        f"No se pudo leer el plan del servidor ({last_error}).",
        "Verifica que `python3 cluster.py server` este ejecutandose "
        "y que ambos nodos esten en la misma red.",
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
    client.set_defaults(func=cmd_client)

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

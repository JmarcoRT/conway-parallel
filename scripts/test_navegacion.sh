#!/usr/bin/env bash
# Prueba reproducible de navegacion S5.
# Genera evidencia en evidencias/ y verifica con grep que las condiciones
# clave se cumplen (salida con codigo != 0 si algo falla).
# Simula un usuario por pipe: cargar, ver secciones, fallar una ruta,
# Enter vacio en el prompt, cancelar con 0 y salir.
set -uo pipefail

cd "$(dirname "$0")/.."

BIN=./build/conway
OUT=evidencias/test_navegacion.txt

if [[ ! -x $BIN ]]; then
    echo "No existe $BIN. Compila primero:" >&2
    echo "  cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

mkdir -p evidencias

printf '%s\n' \
    "1" \
    "patterns/test.txt" \
    "" \
    "1" \
    "" \
    "C" \
    "patterns/inexistente.txt" \
    "C" \
    "" \
    "0" \
    "C" \
    "patterns/test.txt" \
    "2" \
    "" \
    "0" \
    | "$BIN" > "$OUT"
rc=$?

# Copia sin codigos ANSI para poder hacer grep sobre el texto visible
CLEAN=$(mktemp)
sed -e $'s/\x1b\\[[0-9;]*m//g' -e $'s/\x1b\\[[0-9]*[A-Za-z]//g' "$OUT" > "$CLEAN"
trap 'rm -f "$CLEAN"' EXIT

fallos=0

check() {
    local desc="$1"
    local pattern="$2"

    if grep -q -- "$pattern" "$CLEAN"; then
        echo "PASS: $desc"
    else
        echo "FAIL: $desc (no se encontro: $pattern)"
        fallos=$((fallos + 1))
    fi
}

if [[ $rc -eq 0 ]]; then
    echo "PASS: el programa termina con codigo 0"
else
    echo "FAIL: el programa termino con codigo $rc"
    fallos=$((fallos + 1))
fi

check "carga inicial de patterns/test.txt"        "Entrada activa: patterns/test.txt"
check "seccion Benchmark se abre"                 "BENCHMARK"
check "seccion Visualizacion se abre"             "VISUALIZACION"
check "archivo inexistente produce error"         "Error al cargar: No se pudo abrir el archivo: patterns/inexistente.txt"
check "el error indica conservar la entrada"      "se conserva la entrada anterior"
check "Enter vacio no cancela el prompt"          "Escribe una ruta o '0' para cancelar"
check "solo la tecla 0 cancela"                   "Operacion cancelada."
check "cambio de entrada valido"                  "Entrada cargada: patterns/test.txt"

ultimo=$(grep -o "Entrada activa: [^ ]*" "$CLEAN" | tail -1)
if [[ "$ultimo" == "Entrada activa: patterns/test.txt" ]]; then
    echo "PASS: la entrada activa se conserva hasta el final"
else
    echo "FAIL: la entrada activa final es '$ultimo'"
    fallos=$((fallos + 1))
fi

echo
if [[ $fallos -eq 0 ]]; then
    echo "TODO OK - evidencia en $OUT"
    exit 0
fi

echo "$fallos verificacion(es) fallida(s) - revisa $OUT"
exit 1

# Evidencia — Semana 4

## Estado revisado

- Fecha: 25 de septiembre de 2026.
- Base: `origin/develop`, commit `adcee7b` (`docs: actualizar README y guía
  de contribución`).
- No se modificaron `src/`, `include/`, interfaces compartidas ni los
  patrones ya existentes (`patterns/test.txt`).
- Se agregaron: `patterns/block.txt`, `patterns/blinker.txt`,
  `patterns/glider.txt` (mismos patrones y contenido que
  `origin/test/patrones-entrada`, verificados aquí de forma independiente),
  `patterns/toad.txt`, `patterns/beacon.txt`, `patterns/pulsar.txt`,
  `tests/conway_correctness_tests.cpp`, `tests/run_conway_correctness_tests.sh`,
  `tests/dump_board.cpp`, `tests/compare_boards.py`.

## Entorno

- Linux x86_64, kernel `7.1.10-200.fc44.x86_64a`.
- `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`.
- Python `3.12.3` (usado solo por `tests/compare_boards.py`, sin dependencias externas).
- C++20; `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`. Compilación
  directa sin advertencias.
- No se instaló ni se probó OpenMP/MPI en este entorno: los binarios de
  esta entrega compilan `core` e `input` directamente (igual que
  `tests/run_input_tests.sh`), sin pasar por `CMakeLists.txt`, así que no
  dependen de esas bibliotecas. No se hicieron mediciones de rendimiento.

## Resultado: suite de corrección del motor

Comando ejecutado desde la raíz:

```bash
bash tests/run_conway_correctness_tests.sh
```

Salida real completa:

```text
PASS block_stable_2_generations
PASS blinker_period_2
PASS toad_period_2
PASS beacon_period_2
PASS glider_moves_after_4_steps
PASS pulsar_period_3_population_signature
PASS equivalence_helper_self_consistency
Resultado: 7 PASS, 0 FAIL
```

Código de salida: **0**.

| Caso | Resultado |
|---|---|
| Block permanece estable 2 generaciones | PASS |
| Blinker: vertical → horizontal → vertical (periodo 2) | PASS |
| Toad: fase A → fase B → fase A (periodo 2, 6 vivas siempre) | PASS |
| Beacon: densa (8) → dispersa (6) → densa (periodo 2) | PASS |
| Glider: se desplaza (+1,+1) tras 4 pasos, 5 vivas siempre | PASS |
| Pulsar: firma de población 48→56→72→48 (periodo 3) | PASS |
| Auto-consistencia del criterio de equivalencia (§4.3) | PASS |

## Resultado: flujo de volcado y comparación (`dump_board` + `compare_boards.py`)

Comandos ejecutados:

```bash
g++ -std=c++20 -Wall -Wextra -Iinclude \
    tests/dump_board.cpp src/input/input.cpp src/core/game.cpp -o build/dump_board
./build/dump_board patterns/pulsar.txt 3 > /tmp/a.txt
./build/dump_board patterns/pulsar.txt 3 > /tmp/b.txt
python3 tests/compare_boards.py --require-generation 3 /tmp/a.txt /tmp/b.txt
```

Salida real:

```text
EQUIVALENTE: 17x17, generacion 3, 48 vivas
```

Código de salida: **0**.

Además, durante el desarrollo se comprobó manualmente que `compare_boards.py`
detecta correctamente los dos tipos de discrepancia que debe detectar:

- **generación distinta** (comparar generación 3 contra generación 2 del
  mismo patrón): reporta `NO EQUIVALENTE`, señala la diferencia de
  generación, lista las celdas discrepantes y el conteo de vivas distinto;
  código de salida 1.
- **una sola celda alterada a mano** en un volcado idéntico por lo demás:
  reporta `NO EQUIVALENTE` señalando exactamente la celda `(1,0)`; código de
  salida 1.

Esto confirma que el comparador no solo acepta el caso positivo, sino que
efectivamente falla cuando corresponde — condición necesaria para que sirva
como oráculo de equivalencia una vez existan los motores paralelos.

## Cobertura frente al encargo

| Pedido | Dónde |
|---|---|
| Casos de prueba conocidos (Block, Blinker, Glider) | `docs/TESTING.md` §3, `tests/conway_correctness_tests.cpp` |
| Casos adicionales (Toad, Beacon, Pulsar) | Idem |
| Entrada inicial de cada caso | `patterns/*.txt` |
| Número de generaciones a ejecutar | `docs/TESTING.md` §3 (columna dedicada) |
| Resultado esperado | `docs/TESTING.md` §3 + estados exactos en el código del test |
| Criterios para comparar dos ejecuciones | `docs/TESTING.md` §4 |
| Automatización | `tests/conway_correctness_tests.cpp` (corrección) + `tests/dump_board.cpp` / `tests/compare_boards.py` (equivalencia entre motores) |
| Instrucciones de reproducción | `docs/TESTING.md` §5 |
| Evidencia de funcionamiento | Este documento |

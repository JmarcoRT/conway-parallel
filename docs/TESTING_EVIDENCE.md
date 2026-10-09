# Evidencia — Semana 4 (actualización tras revisión)

## Estado revisado

- Rama `test/core-validation`, actualizada con `origin/develop` mediante
  `git merge origin/develop` (sin conflictos: `patterns/block.txt`,
  `patterns/blinker.txt` y `patterns/glider.txt` eran idénticos en ambas
  ramas antes de fusionar).
- Cambios de esta revisión, según lo pedido:
  1. Rama actualizada con `develop` (merge limpio, sin conflictos).
  2. Eliminado `secuencial.txt` (artefacto vacío en la raíz).
  3. `tests/run_conway_correctness_tests.sh` corregido: ahora enlaza
     también `src/core/engines/sequential.cpp` y `src/core/bs23.cpp`
     (el `core` se refactorizó en `GameState` + `bs23::next_cell_state` +
     `engines::step_sequential`; sin esos dos archivos el link falla con
     `undefined reference to step_sequential`).
  4. `conway_correctness_tests` integrado en `CMakeLists.txt` (mismo
     patrón que `input_tests`/`next_cell_state_tests`/`partition_tests`):
     se construye con `cmake --build` y corre como caso `conway_correctness`
     dentro de `ctest`.
  5. `pulsar_period_3_*` ahora compara el **tablero completo** en cada
     generación (`expectBoard`), no solo `aliveCount`; se mantiene el
     chequeo de la firma de población 48-56-72-48 como verificación
     adicional, no como sustituto.
  6. Quitados de `tests/conway_correctness_tests.cpp` los casos Block,
     Blinker y Glider: ya están cubiertos en `tests/input_tests.cpp` con
     los mismos patrones y los mismos estados esperados (verificado abajo
     antes de quitarlos, para confirmar que no había ninguna discrepancia
     oculta).

## Entorno

- Linux x86_64.
- `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`.
- `cmake version 3.28.3`.
- `Open MPI v4.1.6`, `libopenmpi-dev` (instalados para esta verificación;
  `CMakeLists.txt` los exige con `find_package(... REQUIRED)` para el
  ejecutable `conway`).
- Python `3.12.3` (solo para `tests/compare_boards.py`, sin dependencias externas).

## 1) Merge de `develop` — sin conflictos

```text
$ git merge origin/develop -m "merge: actualizar test/core-validation con develop"
Merge made by the 'ort' strategy.
 66 files changed, 3791 insertions(+), 270 deletions(-)
 ...
```

Antes de fusionar se comprobó que los tres patrones compartidos no
divergían:

```text
$ diff <(git show origin/develop:patterns/block.txt)   <(git show test/core-validation:patterns/block.txt)   → sin diferencias
$ diff <(git show origin/develop:patterns/blinker.txt) <(git show test/core-validation:patterns/blinker.txt) → sin diferencias
$ diff <(git show origin/develop:patterns/glider.txt)  <(git show test/core-validation:patterns/glider.txt)  → sin diferencias
```

## 2) Antes de quitar Block/Blinker/Glider de esta suite

Se compiló `conway_correctness_tests.cpp` **con el core refactorizado**
(`game.cpp` + `engines/sequential.cpp` + `bs23.cpp`) todavía incluyendo
Block/Blinker/Glider, para confirmar que el refactor no cambió el
resultado de ningún caso antes de quitarlos por duplicados:

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

Con eso confirmado, se quitaron los tres casos duplicados y se fortaleció
Pulsar (ver §3 y §4 de `docs/TESTING.md`).

## 3) Build completo + CTest (estado final)

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Salida real:

```text
Test project /home/claude/proj2/build
    Start 1: input_functional
1/5 Test #1: input_functional .................   Passed    0.00 sec
    Start 2: rule_bs23_per_cell
2/5 Test #2: rule_bs23_per_cell ...............   Passed    0.00 sec
    Start 3: conway_correctness
3/5 Test #3: conway_correctness ...............   Passed    0.00 sec
    Start 4: mpi_partition_unit
4/5 Test #4: mpi_partition_unit ...............   Passed    0.01 sec
    Start 5: openmp_vs_sequential
5/5 Test #5: openmp_vs_sequential .............   Passed    0.11 sec

100% tests passed, 0 tests failed out of 5
Total Test time (real) =   0.13 sec
```

Detalle de `conway_correctness` (`ctest -R conway_correctness -V`):

```text
PASS toad_period_2
PASS beacon_period_2
PASS pulsar_period_3_full_state
PASS equivalence_helper_self_consistency
Resultado: 4 PASS, 0 FAIL
```

## 4) Script directo (sin CMake), ya corregido

```text
$ bash tests/run_conway_correctness_tests.sh
PASS toad_period_2
PASS beacon_period_2
PASS pulsar_period_3_full_state
PASS equivalence_helper_self_consistency
Resultado: 4 PASS, 0 FAIL
```

Código de salida: **0**.

## 5) Nota aparte: `tests/run_input_tests.sh` falla hoy por una razón distinta

No es parte de esta tarea y no se modificó, pero quedó registrado porque
se detectó al correr la suite completa: `tests/run_input_tests.sh` compila
primero `input_tests` (31 PASS, 0 FAIL — sin relación con este cambio) y
luego intenta compilar `src/main.cpp` enlazando solo `input.cpp` y `core`.
Desde el refactor de `app`/`controls`, `main.cpp` llama a
`conway::app::run()`, que vive en `src/app/app.cpp` y
`src/app/controls.cpp` (no incluidos en ese segundo `g++` del script), así
que el link falla:

```text
/usr/bin/ld: ... undefined reference to `conway::app::run()'
```

El propio script termina con código de salida 1 por esto, después de
reportar las 31 pruebas de `input_tests` en verde. Vale la pena que lo
revise quien mantenga `tests/run_input_tests.sh`.

## 6) Flujo de equivalencia (`dump_board` + `compare_boards.py`), con las fuentes corregidas

```bash
g++ -std=c++20 -Wall -Wextra -Iinclude \
    tests/dump_board.cpp src/input/input.cpp src/core/game.cpp \
    src/core/engines/sequential.cpp src/core/bs23.cpp -o build/dump_board
./build/dump_board patterns/pulsar.txt 3 > /tmp/a.txt
./build/dump_board patterns/pulsar.txt 3 > /tmp/b.txt
python3 tests/compare_boards.py --require-generation 3 /tmp/a.txt /tmp/b.txt
```

Salida real: `EQUIVALENTE: 17x17, generacion 3, 48 vivas` — código de
salida 0. (El comportamiento de detección de discrepancias —generación
distinta, celda alterada— ya se había verificado en la entrega anterior y
no cambió.)

## Cobertura frente a lo pedido en esta revisión

| Pedido | Estado |
|---|---|
| 1. Actualizar `test/core-validation` con `develop` | Hecho — merge limpio, sin conflictos (§1) |
| 2. Eliminar `secuencial.txt` | Hecho |
| 3. Corregir `run_conway_correctness_tests.sh` (enlazar `engines/sequential.cpp` y `bs23.cpp`) | Hecho y verificado (§4) |
| 4. Integrar `conway_correctness_tests` en CMake/CTest | Hecho y verificado (§3) |
| 5. Fortalecer Pulsar (tablero completo, no solo `aliveCount`) | Hecho — `expectBoard` en las 4 generaciones |
| 6. Quitar duplicados de Block/Blinker/Glider | Hecho — verificados antes de quitarlos (§2), ahora solo en `tests/input_tests.cpp` |

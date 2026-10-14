# Validación independiente secuencial vs OpenMP (Semana 7)

Comprueba que el motor OpenMP integrado en `conway::Game`
(`Engine::OpenMP`, ver [OPENMP_INTEGRATION.md](OPENMP_INTEGRATION.md)) produce
**exactamente** el mismo resultado que el secuencial con 1, 2, 4 y 8 hilos.
Es una validación de **corrección y equivalencia**: no mide tiempos, no
calcula speedup ni eficiencia, y no decide qué cantidad de hilos es más rápida.

Alcance deliberado: no se reimplementó ni modificó `step_openmp`, no se tocó la
regla B3/S23 ni la representación del tablero, y no se validó MPI ni Pthreads.

## Qué hay de nuevo y qué se reutilizó

| Pieza | Estado |
|---|---|
| `tests/compare_boards.py` | **Sin cambios.** Aplica los criterios de equivalencia. |
| `tests/dump_board.cpp` | **Adaptado lo mínimo** (ver abajo). Formato de salida intacto. |
| `tests/run_openmp_validation.sh` | Nuevo. Orquesta casos × hilos usando las dos piezas anteriores. |
| CTest `openmp_equivalence_files` | Nuevo. Ejecuta el script con el `dump_board` construido por CMake. |
| `docs/evidence/openmp_equivalence_*.txt` | Evidencia: la pasada principal la genera el propio script; el control negativo es ese mismo script contra copias mutadas. |

No se creó otro mecanismo de comparación.

### Cambios en `dump_board.cpp`

Eran necesarios solo para poder *distinguir y ejecutar* ambos motores:

- `--engine seq|openmp` (por defecto `seq`: el uso original de dos argumentos
  sigue funcionando igual). Llama a `Game::setEngine`.
- `--require-threads N` (solo con `openmp`): antes de evolucionar mide el
  tamaño real del equipo OpenMP con una región paralela de sonda y termina con
  código 3 si no es N. Evita validar "con 8 hilos" corriendo en realidad con
  menos (por ejemplo, por `OMP_THREAD_LIMIT`) y pasar sin ejercitar lo pedido.
- Recuenta las células vivas celda a celda y termina con código 4 si no
  coincide con `aliveCount()`; así el criterio de "cantidad de vivas" no
  depende solo del contador interno del motor.
- Los metadatos (motor, hilos) salen **solo por stderr**. `stdout` mantiene el
  contrato `ancho alto generación vivas` + filas, idéntico entre motores, por lo
  que `compare_boards.py` no necesita cambios.

El número de hilos **no** se fija dentro de `dump_board`: se pasa con la
variable `OMP_NUM_THREADS`, el otro camino documentado en `Game`. Esto
complementa a `game_openmp_vs_sequential`, que compara en memoria y fija los
hilos con `omp_set_num_threads`.

## Casos validados

Cada combinación se ejecuta con **1, 2, 4 y 8 hilos**: 7 patrones × 3
generaciones × 4 hilos = **84 comparaciones** por pasada. Para cada
(patrón, generaciones) se genera **una** referencia secuencial y las cuatro
ejecuciones OpenMP se comparan contra ella. Entrada y número de generaciones
son idénticos en ambos lados; el log registra el `sha256` de cada archivo de
entrada.

| Patrón | Tablero (alto×ancho) | Generaciones | Vivas de referencia (por generación) |
|---|---|---|---|
| `block` | 4×4 | 1, 2, 10 | 4, 4, 4 |
| `blinker` | 5×5 | 1, 2, 25 | 3, 3, 3 |
| `toad` | 6×6 | 1, 2, 25 | 6, 6, 6 |
| `beacon` | 6×6 | 1, 2, 25 | 6, 8, 6 |
| `glider` | 6×7 | 4, 12, 40 | 5, 4, 4 |
| `pulsar` | 17×17 | 1, 3, 30 | 56, 48, 48 |
| `gosper_glider_gun` | 18×50 | 1, 30, 120 | 39, 41, 47 |

Notas sobre la elección:

- No se exige que el patrón conserve su forma "ideal": se exige que **ambos
  motores coincidan**. Por eso el `glider` se ejecuta también hasta chocar con
  el borde muerto (12 y 40 generaciones) y el cañón de Gosper hasta 120: el
  comportamiento en el borde forma parte del contrato que ambos deben cumplir.
- El alto de los tableros (4, 5, 6, 17, 18 filas) no es múltiplo de 8, así que
  con 8 hilos el reparto de filas es desigual y hay hilos con poco o ningún
  trabajo.
- Las generaciones de referencia de Block/Blinker/Toad/Beacon/Pulsar coinciden
  con los periodos conocidos documentados en [TESTING.md](TESTING.md).
- `patterns/test.txt` no se usa: es una copia de `blinker`.
- Los tableros aleatorios densos y grandes ya están cubiertos, en memoria, por
  `game_engine_tests` (37×53 y 200×150); esta validación no los duplica.

## Criterios de equivalencia y dónde se comprueba cada uno

| Criterio | Dónde se comprueba |
|---|---|
| Mismo tablero inicial | Mismo archivo de `patterns/` para ambos lados (`sha256` en el log) |
| Mismo número de generaciones | `compare_boards.py --require-generation G`: ambos volcados deben estar en G |
| Mismas dimensiones | `compare_boards.py` |
| Mismo estado en cada celda | `compare_boards.py` |
| Misma cantidad de vivas | `compare_boards.py` y recuento celda a celda en `dump_board` |
| El equipo OpenMP fue realmente de N hilos | `dump_board --require-threads N` |

## Cómo repetir la validación

Con CTest (forma recomendada; compila `dump_board` con el resto del proyecto):

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R openmp_equivalence_files --output-on-failure -V
```

Sin CMake (solo `g++` con OpenMP y Python 3; compila `dump_board` en un
directorio temporal):

```bash
bash tests/run_openmp_validation.sh
```

Variables opcionales del script: `REPEATS=N` (repite cada caso OpenMP N veces),
`EVIDENCE_OUT=ruta` (además de stdout, guarda la evidencia con cabecera de
entorno), `CXX`, `PYTHON`, y `--bin RUTA` para usar un `dump_board` ya
compilado. Para una corrida más exigente en una máquina multinúcleo:

```bash
REPEATS=20 EVIDENCE_OUT=docs/evidence/openmp_equivalence_multicore.txt \
    bash tests/run_openmp_validation.sh
```

El script fija `OMP_DYNAMIC=FALSE` y elimina `OMP_THREAD_LIMIT`,
`OMP_SCHEDULE`, `OMP_PROC_BIND` y `OMP_PLACES` heredados, para que "N hilos"
sea realmente N.

Una comparación manual de un caso:

```bash
OMP_DYNAMIC=FALSE OMP_NUM_THREADS=4 ./build/dump_board patterns/pulsar.txt 30 \
    --engine openmp --require-threads 4 > /tmp/omp.txt
./build/dump_board patterns/pulsar.txt 30 --engine seq > /tmp/seq.txt
python3 tests/compare_boards.py --require-generation 30 /tmp/seq.txt /tmp/omp.txt
```

**Resultado esperado:** `Resultado: 84 EQUIVALENTE, 0 NO EQUIVALENTE/ERROR`
(multiplicar por `REPEATS` si se usa), código de salida 0 y la línea
`OpenMP es equivalente al secuencial en todos los casos ejecutados`. En
`compare_boards.py`, `EQUIVALENTE: <ancho>x<alto>, generacion G, V vivas`.
Nótese que los logs imprimen el tablero como **ancho×alto** (cabecera de
`dump_board`), mientras que las tablas de esta documentación y los archivos de
`patterns/` usan alto×ancho.

## Evidencia obtenida

- [`evidence/openmp_equivalence_1core.txt`](evidence/openmp_equivalence_1core.txt):
  pasada completa con `REPEATS=3` (252 comparaciones) sobre el commit indicado
  en su cabecera, con el árbol de trabajo limpio.
  Resultado: **252 EQUIVALENTE, 0 NO EQUIVALENTE/ERROR**. Cada línea registra
  patrón, generaciones, hilos, repetición, dimensiones, vivas y `sha256` de la
  entrada.
- [`evidence/openmp_equivalence_negative_control.txt`](evidence/openmp_equivalence_negative_control.txt):
  **control negativo**. Se aplicaron dos mutaciones a *copias* de
  `src/core/engines/openmp.cpp` en `/tmp` (el repositorio no se modifica) y se
  ejecutó la misma validación:
  - M1, el bucle de filas omite la última fila (`r <= height` → `r < height`):
    detectado, **12 de 84** comparaciones fallan (`glider` a 12 y 40
    generaciones y `gosper_glider_gun` a 120), con las celdas discrepantes.
  - M2, el contador de vivas acumula mal: detectado, 80 de 84 fallan por
    recuento celda a celda.

  Esto muestra que la validación puede fallar. También muestra que la
  sensibilidad depende de los casos: M1 no afecta a los patrones pequeños sin
  células en la última fila, por eso se usan varios patrones y generaciones
  largas hasta el borde.

Para reproducir las mutaciones:

```bash
cp src/core/engines/openmp.cpp /tmp/openmp_m1.cpp
sed -i 's/r <= height; ++r/r < height; ++r/' /tmp/openmp_m1.cpp
g++ -std=c++20 -O2 -fopenmp -Iinclude tests/dump_board.cpp src/input/input.cpp \
    src/core/game.cpp src/core/engines/sequential.cpp /tmp/openmp_m1.cpp \
    src/core/bs23.cpp -o /tmp/dump_m1
bash tests/run_openmp_validation.sh --bin /tmp/dump_m1   # debe terminar con código 1
```

## Limitaciones de esta evidencia

- **La evidencia incluida se obtuvo en un equipo de 1 núcleo lógico**
  (`nproc = 1`, ver cabecera). Con un solo núcleo los hilos se intercalan pero
  no corren de verdad en paralelo, así que **no se ejercitan carreras de datos
  reales**. Resultado válido: en los casos probados, la partición de filas, la
  reducción y el borde producen el resultado correcto con 1, 2, 4 y 8 hilos. Resultado no demostrado: ausencia de
  carreras en ejecución paralela real. Conviene repetir la validación con
  `REPEATS` alto en una máquina multinúcleo y guardar la salida como
  `docs/evidence/openmp_equivalence_multicore.txt`.
- `--require-threads` usa una región paralela de sonda: comprueba que el
  entorno concede N hilos, no instrumenta el interior de `step_openmp`.
- Los patrones son pequeños y poco densos; no sustituyen a los tableros
  aleatorios grandes de `game_engine_tests`.
- No hay garantía sobre otros compiladores ni versiones de OpenMP además de los
  indicados en la cabecera de la evidencia.

## Si aparece una divergencia

No se encontró ninguna. Si apareciera, el procedimiento es documentar el caso
mínimo (patrón, generaciones, hilos, repetición y volcados de ambos motores, que
`run_openmp_validation.sh` ya imprime) y comunicarlo antes de modificar el
motor.

## Conclusión

En todos los casos probados (7 patrones, 3 generaciones por patrón, 1, 2, 4 y
8 hilos, 252 comparaciones) el motor OpenMP integrado produjo dimensiones,
generación alcanzada, estado de cada celda y cantidad de células vivas
idénticos a los del motor secuencial, y la validación demostró ser capaz de
detectar un motor defectuoso. La equivalencia queda establecida para estos
casos en un entorno de 1 núcleo; la comprobación con paralelismo real queda
pendiente de ejecutarse en una máquina multinúcleo con las instrucciones de
arriba.

## Fuentes

- Código y documentación de `develop`: `Game`, `Engine`, `step_openmp`,
  `docs/OPENMP_INTEGRATION.md`, `docs/TESTING.md`, `tests/game_engine_tests.cpp`.
- Variables de entorno de OpenMP (`OMP_NUM_THREADS`, `OMP_DYNAMIC`,
  `OMP_THREAD_LIMIT`) y `omp_get_num_threads`: especificación de OpenMP,
  https://www.openmp.org/specifications/ (la misma referencia que usa
  OPENMP_INTEGRATION.md).
- Periodos de los patrones: LifeWiki (ver TESTING.md).
- Apoyo de una herramienta de IA para redactar código y documentación; revisado
  y verificado según `CONTRIBUTING.md` §11.

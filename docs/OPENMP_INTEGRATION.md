# OpenMP integrado y validado (Semana 7)

> Registro histórico de la revisión indicada, no guía del HEAD actual.
> Para comandos y estado vigente, consultar [el índice](README.md) y
> [pruebas actuales](TESTING.md). No reinterpretar las salidas originales como
> resultados del HEAD vigente.


Integra el motor `core::step_openmp(GameState&)` (S6, sin cambios) en la
fachada `conway::Game`, valida que produce los mismos resultados que el
secuencial a través de `Game` con 1, 2, 4 y 8 hilos y deja una medición inicial
reproducible. No se reescribió `step_openmp`, no se duplicó la regla B3/S23 y no
se creó otra representación del tablero.

## Cómo se selecciona el motor

`include/core/game.hpp` agrega un selector mínimo, solo aditivo (constructor y
resto de la API sin cambios):

```cpp
enum class Engine { Sequential, OpenMP };

game.setEngine(conway::Engine::OpenMP);   // desde la siguiente llamada a step()
game.engine();                            // motor activo (por defecto Sequential)
```

- `Game::step()` hace un `switch` y delega en `core::step_sequential` o en
  `core::step_openmp`. `Game` no incluye `omp.h` ni usa pragmas.
- El motor por defecto es `Engine::Sequential`: quien no llama a `setEngine`
  conserva exactamente el comportamiento anterior.
- Se puede cambiar de motor entre generaciones: `setEngine` no toca el tablero,
  `generation` ni `aliveCount`.
- El número de hilos **no** forma parte de `Game`: lo controla quien llama con
  `omp_set_num_threads(n)` o con la variable `OMP_NUM_THREADS`.

Uso mínimo:

```cpp
conway::Game game(input);
game.setEngine(conway::Engine::OpenMP);
omp_set_num_threads(4);
for (int g = 0; g < 100; ++g) game.step();
```

En la revisión S7, la opción **Benchmark** era un placeholder: no hizo falta
tocarla para esta integración (`src/app` no cambia).

## Archivos

| Archivo | Cambio |
|---|---|
| `include/core/game.hpp`, `src/core/game.cpp` | `enum class Engine`, `setEngine()`, `engine()` y selección en `step()` |
| `tests/game_engine_tests.cpp` | Validación OpenMP vs secuencial **a través de `Game`** |
| `tests/run_game_engine_tests.sh` | Compila y ejecuta esa validación sin CMake ni MPI |
| `tests/openmp_timing.cpp` | Herramienta de medición inicial (exploratoria) |
| `scripts/measure_openmp.sh` | Compila y ejecuta la medición y guarda la evidencia |
| `CMakeLists.txt` | Targets `game_engine_tests` y `openmp_timing`, tests de CTest, y `openmp.cpp` añadido a los targets que enlazan `game.cpp` |
| `tests/run_input_tests.sh` | Añade `openmp.cpp` y `-fopenmp` (porque `game.cpp` ahora referencia `step_openmp`) |
| `docs/evidence/openmp_game_validation.txt`, `docs/evidence/openmp_timing.txt` | Evidencia real |

**Impacto en el enlazado:** como `game.cpp` ahora llama a `step_openmp`, todo
ejecutable que enlace `game.cpp` debe incluir también `src/core/engines/openmp.cpp`
(y conviene enlazar `OpenMP::OpenMP_CXX`). Ya está hecho en `conway`,
`input_tests` y en los targets nuevos.

## Validación a través de `conway::Game`

`tests/game_engine_tests.cpp` usa solo la API pública de `Game`. Para cada caso
crea dos `Game` desde el mismo `InputData` (uno con el motor por defecto y otro
con `Engine::OpenMP`) y, **tras cada generación**, compara dimensiones,
`generation`, `aliveCount` (también recontado celda a celda) y todas las celdas.
Se repite con **1, 2, 4 y 8 hilos**, comprobando además que OpenMP realmente
creó un equipo de ese tamaño (`equipo_real`).

Casos: patrones `block`, `blinker` y `glider` de `patterns/`; aleatorios 37×53
(50 gen.) y 200×150 (30 gen.) con semilla fija; tablero lleno 16×16; una fila
64×1 y una columna 1×64 (más hilos que filas). Pruebas adicionales: el motor por
defecto es el secuencial, Block estable, Blinker de periodo 2 y Glider
desplazado (+1,+1) tras 4 generaciones usando solo OpenMP, cambio de motor entre
generaciones, `setCell`/`clear` con OpenMP, y que la entrada original no se
modifica.

Se conserva `tests/openmp_vs_sequential.cpp` (S6, a nivel de `GameState`) sin
cambios.

## Compilar y ejecutar

Requisitos: `g++` con OpenMP (libgomp). Sin CMake:

```bash
bash tests/run_game_engine_tests.sh                 # validación vía Game
bash tests/run_openmp_prototype.sh                  # prueba de S6 (sigue pasando)
bash scripts/measure_openmp.sh                      # medición inicial -> docs/evidence/openmp_timing.txt
```

Con CMake (requiere también MPI porque el `CMakeLists.txt` lo exige):

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/openmp_timing            # medición completa (el test de CTest es solo un smoke test)
```

Tests de CTest: `input_functional`, `rule_bs23_per_cell`, `mpi_partition_unit`,
`openmp_vs_sequential`, `game_openmp_vs_sequential` (nuevo) y
`openmp_timing_smoke` (nuevo, tablero pequeño; solo comprueba que corre y que los
resultados coinciden).

## Resultado esperado

- `run_game_engine_tests.sh`: código de salida `0` y última línea
  `Resultado: 38 PASS, 0 FAIL`.
- `measure_openmp.sh`: código de salida `0`, columna `identico_al_secuencial` en
  `SI` para todas las filas y última línea
  `Resultado: OK, todos los resultados coinciden con el secuencial`.

## Mediciones iniciales (exploratorias)

No son el benchmark final: no hay CSV, gráficas, throughput ni script de
benchmark definitivo. **Sí** se incluye aquí una tabla de speedup/eficiencia
porque se dispone de un entorno multi-núcleo y se considera información útil
para la planificación.

Configuración fija (valores por defecto de `openmp_timing`):

- Tablero aleatorio 1000×1000, densidad 35 %, semilla 2026 (mismo `InputData` en todas las ejecuciones).
- 100 generaciones por ejecución.
- Motores: secuencial, y OpenMP con 1, 2, 4 y 8 hilos.
- 1 ejecución de calentamiento sin medir y **5 a 7 repeticiones medidas** por configuración; el tiempo representativo es la **mediana** (también se muestran mínimo y máximo).

Método:

- El tiempo se toma con `std::chrono::steady_clock` y cubre **solo** el bucle de `game.step()`. La construcción del `Game` y la validación quedan fuera de la región temporizada (`ARCHITECTURE.md`, sección 9).
- Fuera del tiempo se comprueba que generación, `aliveCount` y un checksum del tablero final coinciden con los del secuencial.
- Se puede cambiar la configuración con `--width`, `--height`, `--generations`, `--repeats` y `--seed`.

### Evidencia en entorno de **1 núcleo** (`docs/evidence/openmp_timing.txt`)

Esta es la medición original entregada por Álvaro en una máquina con un solo
núcleo lógico. Con un solo núcleo no se observa aceleración real y los
tiempos no sirven para evaluar el escalado; el archivo se conserva como
evidencia de que el binario compila y produce resultados correctos en
1 hilo.

```text
Fecha: 2026-10-07 18:03 UTC
Compilador: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
Nucleos logicos (nproc): 1
CPU: Intel(R) Xeon(R) Processor @ 2.10GHz

motor       hilos     mediana_ms      min_ms      max_ms  identico_al_secuencial
secuencial  1              463.3       450.9       473.2  SI
openmp      1              409.9       409.1       410.7  SI
openmp      2              409.3       407.3       413.7  SI
openmp      4              408.5       404.5       501.1  SI
openmp      8              418.6       418.4       421.8  SI
```

### Evidencia en entorno **multi-núcleo** (`docs/evidence/openmp_timing_multicore.txt`)

Re-medición realizada durante la integración en S7, en una máquina con
20 procesadores lógicos visibles (i7-12700F) corriendo Ubuntu 24.04
dentro de WSL2 sobre Windows. **Esta es la única evidencia de la rama
`feature/openmp-s7` apta para discutir speedup/eficiencia.**

```text
Fecha: 2026-10-10 21:18 UTC
Compilador: g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
Nucleos logicos (nproc): 20
CPU: 12th Gen Intel(R) Core(TM) i7-12700F

motor       hilos     mediana_ms      min_ms      max_ms  identico_al_secuencial
secuencial  1              658.5       644.7       721.5  SI
openmp      1              616.8       611.7       636.5  SI
openmp      2              323.0       318.8       352.3  SI
openmp      4              165.6       162.2       223.4  SI
openmp      8              124.7       112.9       135.6  SI

Resumen (speedup y eficiencia frente al secuencial = 658.5 ms)

  hilos   mediana_ms   speedup_x   eficiencia_pct
  secuencial  658.5        1.00         100.0
  1         616.8        1.07         106.7
  2         323.0        2.04         102.0
  4         165.6        3.98          99.4
  8         124.7        5.28          66.0
```

Lectura de los resultados (sin asumir mejoras donde no las hay):

- **1 hilo** ≈ **secuencial**: como se espera (1 hilo = sin paralelismo real).
  No se demuestra aquí la causa ni significancia de esa diferencia.
- **2 hilos**: speedup de 2.04x, eficiencia 102 %. La localidad de caché sería una hipótesis; no se midieron
  fallos de caché para demostrarla.
- **4 hilos**: speedup de 3.98x, eficiencia 99.4 % (escalado prácticamente
  lineal, el mejor resultado de la prueba).
- **8 hilos**: speedup de 5.28x, eficiencia 66 %. La ganancia marginal observada es menor; estos tiempos
  no demuestran contención del host ni qué clases de CPU ejecutaron los hilos.

Estos números son exploratorios. La forma de la curva (2x, 4x, 8x con
rendimientos decrecientes) es coherente con la memoria como cuello de botella
a 8 hilos para un tablero de 1000×1000, y con el overhead de OpenMP en
WSL2. Para extraer conclusiones firmes hay que repetir las mediciones
varias veces, en una máquina con CPUs dedicados y midiendo también con
12 y 20 hilos.

### Limitaciones de la evidencia (leer antes de interpretar los tiempos)

- Las cifras de **1 núcleo** (Álvaro) no muestran aceleración y los tiempos
  no sirven para evaluar el escalado. Que OpenMP con 1 hilo salga más rápido
  que el secuencial no se ha investigado; no debe tomarse como una conclusión.
- Las cifras **multi-núcleo** (esta rama) sí muestran aceleración real hasta
  4 hilos (≈ 4x). La tabla llega a 8 hilos y no acredita comportamiento por encima de ese
  número. Mezcla de clases de CPU o contención del host son hipótesis sin
  observaciones de afinidad/topología o contadores que las demuestren. Para resultados más firmes se necesitan más repeticiones, en una
  máquina con CPUs dedicados, midiendo también con 12 y 20 hilos.
- Por la misma razón, la validación con 1 núcleo no ejercita condiciones de
  carrera reales: al quitar a propósito la cláusula `reduction` de
  `step_openmp` las pruebas siguen pasando con 1 núcleo, mientras que un error
  de lógica (recortar el bucle a `r < height`) sí es detectado también a
  través de `Game` (25 de 38 casos fallan). Conviene ejecutar la validación
  varias veces en una máquina multi-núcleo.
- `OMP_THREAD_LIMIT` menor que 8 hace fallar la comprobación `equipo_real`, a
  propósito.
- En esta rama S7 sí se pudo ejecutar CMake/CTest (con `find_package(OpenMP)`
  + `find_package(MPI)` activados, MPI 5.0.10 en
  `/opt/openmpi-5.0.10-gcc14`): 7/7 PASS. Ver
  `tests/run_game_engine_tests.sh` para la validación reproducible vía
  script bash (38 PASS, 0 FAIL).
- `tests/run_input_tests.sh` ya fallaba en `develop` **antes** de S7 en su
  segunda parte (compila `src/main.cpp` sin `src/app/*` y da
  `undefined reference to conway::app::run()`); la suite C++ de ese script
  (32 PASS, 0 FAIL, incluyendo el nuevo caso de Gosper Glider Gun) sigue
  pasando. La causa es que la rama S5 (`feature/controls`, no integrada
  todavía) modificó `src/main.cpp` para llamar a `conway::app::run()` y el
  script no se actualizó en consecuencia. La corrección pertenece a la
  integración de S5, no a S7.

## Qué no se hizo (a propósito)

- No se reescribió ni optimizó `step_openmp`; no se probaron otros `schedule` ni `collapse`.
- En S7 no se integraron CSV, gráficas ni throughput; los speedups y
  eficiencias de arriba son resúmenes exploratorios, no el benchmark final.
- No se conectó el menú **Benchmark** ni se construyó una interfaz de benchmark definitiva.
- No se trabajó en Pthreads ni en MPI.

## Fuentes

- `README.md`, `docs/ARCHITECTURE.md`, `docs/CONTRIBUTING.md`, `docs/OPENMP_PROTOTYPE.md` y el código de `develop` (`Game`, `GameState`, motores, `CMakeLists.txt`).
- Referencia de OpenMP: `omp_set_num_threads`, `omp_get_num_threads` y `omp_set_dynamic` (https://www.openmp.org/specifications/).
- Apoyo de una herramienta de IA para redactar código y documentación; revisado y verificado según `CONTRIBUTING.md` §11.

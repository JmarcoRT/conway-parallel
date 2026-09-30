# Prototipo OpenMP aislado (Semana 6)

Primer prototipo de ejecución de **una generación** con OpenMP sobre el core
existente en `develop`. No está integrado en `Game`, `main` ni en el menú; eso
y las mediciones con 1, 2, 4 y 8 hilos corresponden a S7.

## Archivos

| Archivo | Rol |
|---|---|
| `include/core/engines/openmp.hpp` | Declara `core::step_openmp(GameState&)` |
| `src/core/engines/openmp.cpp` | Motor OpenMP (único archivo con `#pragma omp`) |
| `tests/openmp_vs_sequential.cpp` | Comparación contra `step_sequential` |
| `tests/run_openmp_prototype.sh` | Compila y ejecuta la prueba sin CMake ni MPI |
| `CMakeLists.txt` | Nuevo target de prueba `openmp_vs_sequential` (solo dentro de `BUILD_TESTING`) |

## Qué se reutiliza del core (sin modificarlo)

- `core::GameState`: mismo estado, doble buffer `current`/`next`, halo de 1 celda, `stride = width + 2`.
- `core::next_cell_state` (`bs23.hpp`): la regla B3/S23 se llama, no se reescribe.
- `core::swap_buffers`: se invoca una vez al final, igual que en el secuencial.
- Misma semántica de `step_sequential`: fija `aliveCount`, hace el swap e incrementa `generation`.

No se tocó ninguna interfaz compartida (`GameState`, `bs23.hpp`, `sequential.hpp`, `Game`, `input`).

## Qué se paraleliza y cómo se reparte el trabajo

Dentro de una generación, el cálculo de cada celda de `next` depende solo de
`current` (generación N), así que las celdas son **independientes entre sí**.
Es el único bucle paralelizado: el recorrido de las filas lógicas `1..height`.

```cpp
#pragma omp parallel for schedule(static) reduction(+ : nextAlive)
for (r = 1; r <= height; ++r)
    for (c = 1; c <= width; ++c) { ... next_cell_state(pIn, stride, r, c) ... }
```

- **Reparto:** por filas, en bloques contiguos (`schedule(static)`). Cada hilo
  procesa filas completas. Con `H` filas y `T` hilos, cada hilo recibe unos `H/T`
  filas consecutivas (reparto desigual si `H` no es múltiplo de `T`; si hay más
  hilos que filas, algunos quedan sin trabajo).
- **Por qué filas:** la memoria es *row-major*, así que cada hilo lee/escribe
  zonas contiguas, y coincide con la estrategia de Pthreads descrita en
  `ARCHITECTURE.md` (rangos de filas), lo que facilita comparar después.

### Datos compartidos y privados

| Dato | Tratamiento |
|---|---|
| `current` (vía `pIn`) | **Compartido, solo lectura.** Nadie lo escribe durante la generación. |
| `next` (vía `pOut`) | **Compartido**, pero cada hilo escribe solo en las filas que le tocaron: regiones disjuntas, sin condiciones de carrera y sin `critical`/`atomic`. |
| `stride`, `width`, `height` | Compartidos, solo lectura (copias `const` locales). |
| `r`, `row`, `c`, `cell` | Privados (declarados dentro del bucle paralelo). |
| `nextAlive` | `reduction(+)`: cada hilo acumula una copia privada y OpenMP las suma al final. |
| `generation`, `aliveCount`, `swap_buffers` | Se actualizan **fuera** de la región paralela, por un solo hilo. |

### Sincronización

`parallel for` tiene una barrera implícita al final: todos los hilos terminan de
escribir `next` antes de `swap_buffers`. Esto cumple lo pedido en
`ARCHITECTURE.md` (leer de `current`, escribir en la región propia de `next`,
sincronizar antes del intercambio).

### Qué no se hizo (a propósito)

- No se fija el número de hilos dentro del motor: lo controla `OMP_NUM_THREADS`
  o `omp_set_num_threads()` del llamador.
- No se probaron otros `schedule`, `collapse`, ni optimizaciones.
- No se integró en `Game::step()` ni en `main` (S7).
- No hay benchmarks.

## Compilar y ejecutar la prueba

Requisitos: `g++` con OpenMP (libgomp). Basta con:

```bash
bash tests/run_openmp_prototype.sh
```

Con CMake (requiere también MPI porque el `CMakeLists.txt` actual lo exige):

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target openmp_vs_sequential
ctest --test-dir build -R openmp_vs_sequential --output-on-failure
```

## Qué hace la prueba

Para cada caso crea dos `GameState` idénticos desde el mismo `InputData`, avanza
uno con `step_sequential` y otro con `step_openmp`, y **tras cada generación**
compara el área lógica de `current`, `generation` y `aliveCount`. Se repite con
1, 2, 4 y 8 hilos (solo por corrección; no mide tiempos).

Casos: `block` (4 gen.), `blinker` (4), `glider` (12), aleatorios 37×53 (50 gen.)
y 200×150 (30 gen.) con semilla fija, y tableros de una fila y de una columna
(más hilos que filas). Además verifica que el Blinker vuelve a su estado inicial
tras 2 generaciones usando solo el motor OpenMP.

## Resultado esperado

Código de salida `0` y la última línea:

```text
Resultado: 29 PASS, 0 FAIL
```

## Evidencia

Salida real de `bash tests/run_openmp_prototype.sh` (Ubuntu 24.04, g++ 13.3,
`-std=c++20 -O2 -fopenmp -Wall -Wextra -Wpedantic -Wconversion -Wshadow`, sin
advertencias):

```text
PASS block hilos=1 generaciones=4 vivas_finales=4
PASS block hilos=2 generaciones=4 vivas_finales=4
PASS block hilos=4 generaciones=4 vivas_finales=4
PASS block hilos=8 generaciones=4 vivas_finales=4
PASS blinker hilos=1 generaciones=4 vivas_finales=3
PASS blinker hilos=2 generaciones=4 vivas_finales=3
PASS blinker hilos=4 generaciones=4 vivas_finales=3
PASS blinker hilos=8 generaciones=4 vivas_finales=3
PASS glider hilos=1 generaciones=12 vivas_finales=4
PASS glider hilos=2 generaciones=12 vivas_finales=4
PASS glider hilos=4 generaciones=12 vivas_finales=4
PASS glider hilos=8 generaciones=12 vivas_finales=4
PASS aleatorio_37x53 hilos=1 generaciones=50 vivas_finales=239
PASS aleatorio_37x53 hilos=2 generaciones=50 vivas_finales=239
PASS aleatorio_37x53 hilos=4 generaciones=50 vivas_finales=239
PASS aleatorio_37x53 hilos=8 generaciones=50 vivas_finales=239
PASS aleatorio_200x150 hilos=1 generaciones=30 vivas_finales=4467
PASS aleatorio_200x150 hilos=2 generaciones=30 vivas_finales=4467
PASS aleatorio_200x150 hilos=4 generaciones=30 vivas_finales=4467
PASS aleatorio_200x150 hilos=8 generaciones=30 vivas_finales=4467
PASS una_fila_64x1 hilos=1 generaciones=10 vivas_finales=0
PASS una_fila_64x1 hilos=2 generaciones=10 vivas_finales=0
PASS una_fila_64x1 hilos=4 generaciones=10 vivas_finales=0
PASS una_fila_64x1 hilos=8 generaciones=10 vivas_finales=0
PASS una_columna_1x64 hilos=1 generaciones=10 vivas_finales=0
PASS una_columna_1x64 hilos=2 generaciones=10 vivas_finales=0
PASS una_columna_1x64 hilos=4 generaciones=10 vivas_finales=0
PASS una_columna_1x64 hilos=8 generaciones=10 vivas_finales=0
PASS blinker_periodo_2_openmp
Resultado: 29 PASS, 0 FAIL
```

El resto de pruebas existentes (`tests/run_input_tests.sh`) sigue pasando.

### Limitaciones de la evidencia

- La prueba se ejecutó en una máquina de 1 núcleo: los hilos OpenMP se crean y se
  reparten las filas, pero no corren realmente en simultáneo. Un error de
  sincronización (p. ej. quitar la cláusula `reduction`) podría no manifestarse
  ahí. Conviene repetir `run_openmp_prototype.sh` en una máquina multinúcleo y,
  si es posible, varias veces.
- Se comprobó que la prueba detecta errores de lógica (al recortar el bucle a
  `r < height` fallan 20 de los 29 casos).
- Entorno de referencia del proyecto: GCC 14.2; aquí se usó g++ 13.3.
- No se pudo probar el build con CMake en este entorno (no estaba instalado);
  el target agregado sigue el patrón de los existentes.

## Fuentes consultadas

- `README.md`, `docs/ARCHITECTURE.md`, `docs/CONTRIBUTING.md` y el código del core en `develop`.
- Especificación de OpenMP (directiva `parallel for`, cláusulas `schedule(static)` y `reduction`): https://www.openmp.org/specifications/
- Apoyo de una herramienta de IA para redactar el código y la documentación; revisado y verificado según `CONTRIBUTING.md` §11.

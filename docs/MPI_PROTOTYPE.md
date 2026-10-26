# Prototipo MPI aislado — procesos y reparto de filas (Semana 6)

Prototipo independiente del módulo `mpi`. Sin opciones conserva la comprobación
de distribución original. Con `--steps N` intercambia halos, evoluciona N pasos
y compara células, generación y población global contra el secuencial en cada paso.
`Game::step()` sigue siendo local. La integración de sesiones y menús se hace
mediante el cliente/trabajador de [MPI_SESSION.md](MPI_SESSION.md); modo didáctico
y experimentos MPI siguen pendientes.

## Evolución independiente

`core::step_mpi(GameState&, RowPartition, MPI_Comm)` vive en
`include/core/engines/mpi.hpp` y `src/core/engines/mpi.cpp`. Reutiliza la partición
y el estado local existentes. Todos los ranks llaman al paso colectivamente.
Intercambia filas de `current` con dos `MPI_Sendrecv` y reutiliza
`step_sequential` (incluida B3/S23, doble buffer e incremento de generación).
Los vecinos se calculan solo entre ranks con filas; los ranks vacíos avanzan
su generación y participan en las colectivas, sin enviar halos.
Los bordes exteriores son muertos: los halos se limpian antes de cada intercambio
y los extremos usan `MPI_PROC_NULL`. `state.aliveCount` sigue siendo local;
el valor devuelto por el paso es la población global obtenida con `Allreduce`.
El comunicador debe ser exclusivo para este flujo, sin otros mensajes con sus tags.

La validación de forma/partición se coordina antes del intercambio. Dimensiones
globales y generación deben coincidir entre ranks; el estado se inicializa con
células binarias y columnas halo cero. El camino Scatterv/Gatherv limita el tablero
a `INT_MAX` células mediante `mpi::checked_cell_count`, que valida por división
antes de multiplicar o convertir. La entrada sintética se valida antes de reservar.
No hay recuperación ante pérdida de un rank; una excepción
inesperada en el ejecutable aborta el trabajo. No se generan trazas ni métricas.

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target mpi_partition_prototype mpi_evolution_tests -j2
timeout --kill-after=5s 45s mpirun --oversubscribe -np 4 \
  ./build/mpi_partition_prototype patterns/glider.txt --steps 30
ctest --test-dir build -R '^mpi_(evolution|prototype_evolution)_' --output-on-failure
```

CTest registra P=1,2,3,4,7,8, cada uno con timeout de 45 segundos. Las pruebas
comparan la generación cero y los 30 pasos siguientes, células y población contra
el recorrido secuencial del tablero completo, más contadores locales y halos
exteriores de ambos buffers en todos los ranks. Incluyen patrones, tableros vacíos/llenos/deterministas,
una fila/columna, repartos desiguales, ranks sin filas y blinkers sobre fronteras.
También comprueban el rechazo colectivo de una partición inconsistente.
La sobresuscripción se habilita con `--oversubscribe` explícito solo en las
órdenes de estas pruebas locales con Open MPI, sin cambiar el simulador.
Dos pruebas adicionales ejecutan el prototipo con `--steps 30`, P=1 y P=8,
con tres filas (incluidos procesos vacíos); también tienen timeout de 45 segundos.

Las secciones siguientes describen el modo original de distribución y su evidencia histórica.

## Qué comprueba

1. `MPI_Init` / `MPI_Finalize` correctos; cada proceso conoce su `rank` y el `size`.
2. Partición horizontal reproducible, contigua y balanceada, también cuando las filas no se
   dividen exactamente entre procesos y cuando hay más procesos que filas.
3. Ninguna fila se pierde ni se duplica (el tablero reensamblado coincide con la entrada).
4. Cada partición se corresponde con un `core::GameState` local válido (misma representación
   común del core; no hay core alternativo ni se duplica B3/S23).

## Estrategia de reparto

Distribución por bloques de filas contiguas, con las filas sobrantes en los primeros ranks:

```text
base  = alto / size
resto = alto % size
filas(rank)     = base + (rank < resto ? 1 : 0)
primera(rank)   = rank * base + min(rank, resto)      // índice global, base 0
```

Ejemplo: 10 filas y 4 procesos → rank 0 = filas 0–2, rank 1 = 3–5, rank 2 = 6–7, rank 3 = 8–9.

Propiedades (probadas en `tests/partition_tests.cpp` para alto 1..64 y size 1..80):

- bloques contiguos y en orden de rank, sin huecos ni solapes;
- la diferencia de filas entre dos ranks cualesquiera es como máximo 1;
- si `size > alto`, los últimos ranks reciben 0 filas (partición vacía, `height = 0` local);
- es una función pura de `(alto, size, rank)`: mismo resultado en cada ejecución.

## Flujo de ejecución

Sigue el flujo del documento S4 (Scatter al inicio, Gather al final):

1. `MPI_Init`, `MPI_Comm_rank` y `MPI_Comm_size`.
2. Solo rank 0 lee la entrada (o genera el tablero sintético) y difunde alto y ancho con `MPI_Bcast`.
   Si la lectura falla, todos los ranks lo saben por ese mismo `Bcast` y terminan sin colgarse.
3. Rank 0 reparte las franjas con `MPI_Scatterv` (con "v" porque el reparto puede ser desigual).
4. Cada rank arma su `GameState` local con sus filas (`make_local_state_from_rows`).
5. Cada rank informa de su rank, pid, filas y células vivas (`MPI_Gather`); rank 0 reúne las franjas
   con `MPI_Gatherv`, compara con la entrada y suma las vivas con `MPI_Reduce`.
6. `MPI_Finalize`.

## Relación con `GameState`

Cada rank construye su propio `core::GameState` con la forma que describe `ARCHITECTURE.md`:

| Campo | Valor en MPI |
|---|---|
| `width` | ancho global |
| `height` | filas propias del rank (alto local) |
| `stride` | `width + 2` |
| `current` / `next` | `stride * (height + 2)` (1 fila halo arriba y 1 abajo) |
| `generation` | 0 |
| `aliveCount` | vivas de las filas propias |

Las filas propias ocupan las filas locales `1..height`. En esta etapa los halos quedan a 0: el
borde global ya es correcto y las fronteras entre ranks se rellenarán en la etapa de evolución
(`MPI_Sendrecv` de halos), tras lo cual `next_cell_state` y `swap_buffers` se reutilizan tal cual.

## Archivos

| Archivo | Rol |
|---|---|
| `include/mpi/partition.hpp`, `src/mpi/partition.cpp` | `RowPartition` y `partition_rows` (sin llamadas MPI) |
| `include/mpi/local_state.hpp`, `src/mpi/local_state.cpp` | `make_local_state` y `pack_local_rows` sobre `core::GameState` (sin llamadas MPI) |
| `src/mpi/partition_prototype.cpp` | ejecutable MPI `mpi_partition_prototype` |
| `tests/partition_tests.cpp` | pruebas sin MPI (`ctest`: `mpi_partition_unit`) |
| `tests/run_mpi_partition.sh` | matriz de ejecuciones con `mpirun` |

`core` no se modificó y sigue sin depender de MPI; la dependencia con MPI está solo en
`partition_prototype.cpp`.

## Compilar y ejecutar

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build                 # pruebas sin MPI

mpirun -np 4 ./build/mpi_partition_prototype patterns/glider.txt
mpirun -np 7 ./build/mpi_partition_prototype patterns/glider.txt     # un rank sin filas
mpirun -np 4 ./build/mpi_partition_prototype --synthetic 10 13       # 10 filas, 13 columnas
mpirun -np 4 ./build/mpi_partition_prototype --synthetic 101 64 --show

tests/run_mpi_partition.sh             # varios np y tableros; devuelve 0 si todo OK
```

Si hay menos núcleos que procesos, Open MPI necesita `--oversubscribe` (el script lo añade por
defecto; se cambia con `MPIRUN_FLAGS`). Uso: `[archivo.txt | --synthetic ALTO ANCHO] [--show]`;
las filas se imprimen automáticamente si el alto es ≤ 16. El código de salida es 0 solo si todas las
verificaciones pasan.

## Resultado esperado

Con `mpirun -np 4 ./build/mpi_partition_prototype patterns/glider.txt` (6 filas, 7 columnas):

```text
rank     pid  filas globales  n_filas  vivas  stride  buffer(bytes)
   0    ....          [0..1]        2      1       9             36
   1    ....          [2..3]        2      4       9             36
   2    ....          [4..4]        1      0       9             27
   3    ....          [5..5]        1      0       9             27
...
[OK] ... (9 verificaciones)
RESULTADO: OK
```

La columna `pid` cambia en cada ejecución (son procesos distintos del SO); el resto es determinista.

## Evidencia de funcionamiento

Ejecución propia en WSL (Ubuntu 24.04): [`docs/evidence/mpi_partition_wsl.txt`](evidence/mpi_partition_wsl.txt)
(salida completa de `mpirun -np 4` con `glider.txt`, y resumen de la matriz de `tests/run_mpi_partition.sh`).

- `ctest`: 4/4 pruebas pasan (`input_functional`, `rule_bs23_per_cell`, `mpi_partition_unit` y
  `openmp_vs_sequential`; esta última no es de este entregable, pero forma parte de la suite).
- `tests/run_mpi_partition.sh`: 22 ejecuciones (np = 1, 2, 3, 4, 7, 8; patrones y tableros
  sintéticos, incluidos 3 filas con 4 y 8 procesos) terminan con `RESULTADO: OK`.

Verificaciones que hace el ejecutable (rank 0, tras `MPI_Scatterv`/`MPI_Gather`/`MPI_Gatherv`/`MPI_Reduce`):
identificación de cada rank y pids distintos; contigüidad; suma de filas = alto; balance (max−min ≤ 1);
coincidencia con `partition_rows`; tablero reensamblado == entrada; vivas totales == entrada;
invariantes del `GameState` local (todos los ranks, con `MPI_Allreduce`).

## Decisiones técnicas

- Solo rank 0 lee el archivo; el reparto usa `MPI_Scatterv`, como propone el documento S4. Los conteos
  de MPI son `int`, por lo que se rechaza un tablero con más de `INT_MAX` celdas (error limpio).
- Los errores de entrada se comunican a todos los ranks con el mismo `MPI_Bcast` que difunde las
  dimensiones (`dims[0]` = indicador de fallo), para que todos terminen limpio sin quedar bloqueados
  en colectivas. `MPI_Allreduce` (`MPI_MIN`) se usa solo para saber si el `GameState` local de todos
  los ranks es válido.
- Se permiten particiones vacías en lugar de rechazar `size > alto`.
- Se añadieron `include/mpi/` y `src/mpi/` (módulo `mpi` ya previsto en `ARCHITECTURE.md`); no se
  cambió ninguna interfaz compartida.

## Entorno y fuentes

- Probado con Open MPI 4.1.6 y GCC 13.3 (Ubuntu 24.04), **no** con el entorno de referencia
  (Open MPI 5.0.10 / GCC 14.2 en `/opt/openmpi-5.0.10-gcc14`). Solo se usan funciones MPI básicas
  (`Init`, `Comm_rank/size`, `Scatterv`, `Gather`, `Gatherv`, `Reduce`, `Allreduce`, `Bcast`,
  `Get_library_version`), disponibles también en 5.x. Conviene volver a ejecutar
  `tests/run_mpi_partition.sh` en el entorno de referencia.
- Con Open MPI 4.1.6 aparecen avisos `-Wcast-function-type` provenientes de los bindings C++ de
  `mpi.h` (cabeceras del sistema); no vienen del código del proyecto.
- Fuente: documento del equipo "Entregable S4: Investigar estrategia MPI y documentar partición horizontal" (R. Munayco): partición por franjas de filas, halo superior e inferior, flujo Scatter/Gather y MPI_Sendrecv para los halos.

## Diferencias con el documento S4

- El documento menciona Scatter; aquí se usa `MPI_Scatterv` porque el reparto puede ser desigual.
- El documento trata solo el caso divisible; aquí se resuelve el resto dando una fila extra a los primeros ranks y se permiten ranks sin filas.
- El GameState del core también tiene halo en columnas (stride = width + 2), por eso cada rank guarda (filas + 2) x (ancho + 2).

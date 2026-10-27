# Motor MPI: evolución con intercambio de halos

Documento vigente. Continúa el [prototipo de partición](MPI_PROTOTYPE.md), que sigue
disponible como registro y como ejecutable independiente.

## Resumen

| Pieza | Archivo | Depende de MPI |
|---|---|---|
| Partición horizontal | `mpi/partition.hpp` (sin cambios) | No |
| Vecinos de halo | `mpi/halo.hpp` | No |
| Estado local | `mpi/local_state.hpp` (sin cambios) | No |
| Reparto/reensamblado | `mpi/distribution.hpp` (Scatterv/Gatherv) | Sí |
| Motor | `core/engines/mpi.hpp` (`step_mpi`) | Sí |
| Metadatos para UI | `mpi/region_metadata.hpp` (CSV v1) | No |
| Vista textual para UI | `ui/mpi_regions_view.hpp` | No |
| Ejecutable | `src/mpi/mpi_main.cpp` → `conway_mpi` | Sí |

Targets CMake: `conway_mpi_layout` (partición, halos, metadatos) y
`conway_mpi_view` (vista) son C++ estándar y no enlazan MPI; `conway_mpi`
enlaza `MPI::MPI_CXX`. `Game`, `Engine`, GameState y B3/S23 no cambiaron.

## Modelo

Cada rank construye su `core::GameState` local con la forma descrita en
[ARCHITECTURE.md](ARCHITECTURE.md): `height` = filas propias, `stride = width + 2`,
buffers `stride * (height + 2)`. Las filas propias ocupan las filas locales
`1..height`; las filas `0` y `height + 1` son halos.

Antes de cada generación `step_mpi` rellena los halos con las filas frontera de
los vecinos (`mpi::halo_neighbors`):

```text
rank r-1  ... | fila propia final  | ──► halo 0 de r
rank r        | halo 0 | filas 1..h | halo h+1 |
rank r+1  | fila propia inicial | ... ──► halo h+1 de r
```

- `up` = dueño de la fila global `firstRow - 1`; `down` = dueño de `endRow`.
- Sin vecino (`kNoNeighbor = -1` → `MPI_PROC_NULL`): el halo es borde global,
  nunca se escribe y conserva 0 (mismo borde muerto fijo, no periódico, que el
  motor secuencial). La relación es simétrica.
- Un rank sin filas (más procesos que filas) no calcula ni intercambia; el
  último rank con filas tiene `down = kNoNeighbor`.
- Solo viajan las `width` células lógicas de cada fila; las columnas halo
  siguen a 0 en todos los ranks.

Después del intercambio, cada célula usa `next_cell_state` (no se duplica la
regla), se fija `aliveCount` **local**, `swap_buffers` y `++generation`, como
indica CONTRIBUTING.md §7. El total global se pide con `mpi_global_alive()`
(`MPI_Allreduce`) solo cuando hace falta, para no añadir una colectiva por
generación.

## Estrategias de intercambio (`MpiHaloMode`)

| Modo | Secuencia por generación | Uso |
|---|---|---|
| `Blocking` (defecto) | `MPI_Sendrecv` hacia arriba, `MPI_Sendrecv` hacia abajo, recorrido de todas las filas | Referencia simple |
| `Overlap` | `MPI_Irecv` ×2, `MPI_Isend` ×2, filas interiores `2..h-1`, `MPI_Waitall`, filas `1` y `h` | Oculta latencia detrás del cómputo interior |

Ambos producen el mismo tablero (lo comprueban las pruebas). En `Overlap` las
recepciones escriben solo las filas halo, que las filas interiores no leen; los
envíos solo leen filas propias de `current` y el recorrido escribe en `next`.

`MpiStepStats` informa por rank y por generación: segundos en intercambio
(incluida la espera al vecino), segundos de cómputo, mensajes y bytes enviados.
Son observaciones para decidir optimizaciones; no sustituyen el cronómetro del
lote.

Tráfico por generación: `2 * (ranks con filas - 1)` mensajes de `width` bytes,
independiente del alto del tablero. Por eso la relación cómputo/comunicación
mejora con tableros más altos por rank.

## Ejecutable `conway_mpi`

```bash
mpirun -np 4 ./build/conway_mpi patterns/glider.txt --generations 20 --verify
mpirun -np 4 ./build/conway_mpi --generate 2048 2048 3000 2026 \
       --generations 100 --mode overlap
mpirun -np 2 ./build/conway_mpi patterns/pulsar.txt --regions --metadata /tmp/regiones.csv
```

| Opción | Efecto |
|---|---|
| `archivo.txt` | Entrada con el formato de `loadInput` (por defecto `patterns/glider.txt`) |
| `--generate ALTO ANCHO DENSIDAD SEMILLA` | `input::generateInput`; densidad en 1/10000 |
| `--generations N` | Generaciones (defecto 10) |
| `--mode blocking\|overlap` | Estrategia de halos |
| `--verify` | Oráculo: `step_sequential` sobre el tablero completo, fuera del cronómetro; además exige que las regiones usadas coincidan con los metadatos |
| `--dump RUTA\|-` | Tablero final en el formato de `tests/dump_board.cpp` (`compare_boards.py`) |
| `--metadata RUTA\|-` | CSV de regiones, halos y métricas (sección siguiente) |
| `--regions` | Mapa fila → rank con las filas frontera |

El tiempo informado va de `MPI_Barrier` + `MPI_Wtime` al final del bucle y es el
máximo entre ranks. Excluye lectura, reparto, reensamblado, verificación y E/S.
Código de salida: 0 correcto, 1 error de entrada o verificación, 2 uso.

## Metadatos de regiones y halos para la UI

La UI FTXUI es un proceso sin MPI. Tiene dos formas de obtener la geometría,
ambas sin `mpirun` y sin una segunda fórmula de reparto:

1. `mpi::describe_decomposition(width, height, procesos)`: regiones, halos y
   tráfico por generación derivados de `partition_rows`/`halo_neighbors`.
2. `mpi::read_decomposition_csv(stream)`: lee lo que escribió
   `conway_mpi --metadata`, con métricas reales por rank. Rechaza un archivo cuya
   geometría no coincida exactamente con `describe_decomposition`.

Contrato CSV v1:

```text
# conway-mpi-decomposition v1
width,height,processes,generation,halo_mode
7,6,4,10,overlap
rank,first_row,row_count,halo_top_rank,halo_top_row,halo_bottom_rank,halo_bottom_row,local_buffer_cells,alive,halo_seconds,compute_seconds,messages_sent,bytes_sent
0,0,2,-1,,1,2,36,0,0.000236,1.41e-06,10,70
...
```

Filas globales base 0. `halo_*_rank = -1` con fila vacía = borde global. Las
métricas son acumuladas sobre el lote; si la instantánea solo tiene geometría,
esas columnas quedan vacías. Los segundos usan la representación más corta que
se relee exacta (`std::to_chars`).

`ui/mpi_regions_view.hpp` da formato sin FTXUI ni MPI:

- `mpiRowInfo(layout)`: por fila global, dueño y a qué halo viaja (para colorear
  CURRENT por rank y marcar filas frontera).
- `mpiRegionTableLines(snapshot)`: tabla por rank (filas, origen de halos, métricas).
- `mpiRowMapLines(layout)`: mapa fila → rank; resume interiores si el alto es grande.
- `mpiWorkersSummary(layout)`: una línea para `ExecutionInfo::workersInfo`.

Para usarlo desde la aplicación FTXUI basta enlazar `conway_mpi_view` al target
`conway` y envolver las líneas en `text()`. El panel FTXUI aún no está conectado:
requiere decidir con el equipo de UI dónde se muestra y si se lanza `mpirun`
desde la aplicación o se carga un CSV ya generado.

## Pruebas

| Prueba | Requiere MPI | Contrato |
|---|---|---|
| `mpi_halo_unit` | No | Vecinos correctos y simétricos; emulación del intercambio en un proceso == secuencial en cada generación (6 patrones y sintéticos, 1–13 procesos, ranks vacíos); bordes globales y columnas halo a 0 |
| `mpi_region_metadata` | No | Geometría == partición, tráfico, ida y vuelta CSV exacta, rechazos |
| `mpi_regions_view` | No | Dueños/destinos por fila, tabla, mapa resumido, resumen |
| `mpi_engine_np1/np2/np4` | Sí | Gosper 150 generaciones, `blocking`, `--verify` |
| `mpi_engine_overlap_np1/np2/np4` | Sí | Ídem con `overlap` |
| `mpi_engine_matrix` | Sí | `tests/run_mpi_engine.sh`: 6 casos × np 1/2/4/3/7 × 2 modos; tablero final igual entre todos los np y modos (`compare_boards.py`) |

Las pruebas MPI se registran solo si CMake encuentra `mpiexec`. Si la cabecera
`mpi.h` define `OPEN_MPI` (o `mpiexec --version` identifica Open MPI), se añade
`--oversubscribe` por defecto
(`CONWAY_MPIEXEC_PREFLAGS`): sin él, Open MPI rechaza lanzar más procesos que
núcleos (error `prte-rmaps-base:alloc-error`), lo que afecta a los casos de 4 y
7 procesos de `mpi_engine_matrix`. La variable queda en la caché de CMake: tras
cambiarla, reconfigurar con `-DCONWAY_MPIEXEC_PREFLAGS=...` o con un build nuevo.

```bash
cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build -j2
ctest --test-dir build -R mpi_ --output-on-failure
tests/run_mpi_engine.sh build
```

## Medición exploratoria

`scripts/measure_mpi.sh [build] [salida]` ejecuta np 1/2/4 × modos en rondas
alternadas (defecto: 2048×2048, densidad 30 %, semilla 2026, 100 generaciones,
5 repeticiones), verifica cada ejecución y resume medianas y speedup respecto a
np=1 del mismo modo. Sin `salida` no genera evidencia. No es una campaña oficial
del [protocolo](BENCHMARK_PROTOCOL.md): no hay todavía resultados registrados en
el entorno de referencia.

## Estado de la verificación

- Correcto por construcción y probado sin MPI con `mpi_halo_unit`.
- El motor y los scripts se ejercitaron durante el desarrollo con un sustituto
  de MPI basado en hilos (no versionado), compilando con
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` (G++ 13).
- **Pendiente**: ejecutar `ctest -R mpi_` y `tests/run_mpi_engine.sh` con
  Open MPI 5.0.10 / G++ 14 (`/opt/openmpi-5.0.10-gcc14`) y registrar la salida
  en `docs/evidence/`, como se hizo con el prototipo.

## Decisiones técnicas

- El motor vive en `core/engines/` como indica CONTRIBUTING §7; es el único
  archivo del core que incluye `<mpi.h>`. Los vecinos se calculan en el módulo
  `mpi`, así el core no depende de la partición.
- No se añadió `Engine::MPI` a `Game`: Game representa un tablero global en un
  proceso y la TUI no se lanza con `mpirun`. Cambiar esa interfaz requiere
  coordinación (ARCHITECTURE, contratos de integración).
- `aliveCount` local y Allreduce bajo demanda: evita una colectiva de
  sincronización global por generación.
- Se envía solo `width` bytes por fila (sin columnas halo) y no se usan tipos
  derivados: las filas ya son contiguas.
- Posibles optimizaciones no implementadas: peticiones persistentes
  (`MPI_Send_init`/`MPI_Recv_init`, una pareja por buffer porque `swap_buffers`
  alterna punteros), halos de profundidad k para intercambiar cada k
  generaciones y descomposición 2D para muchos procesos.

## Fuentes

- MPI Forum, *MPI: A Message-Passing Interface Standard*, versión 3.1:
  `MPI_PROC_NULL` (§3.11, la operación termina sin modificar el buffer);
  orden sin adelantamiento de mensajes (§3.5); acceso de solo lectura a un
  buffer de envío pendiente permitido desde MPI-3.0 (§3.7); `MPI_Sendrecv`
  para evitar interbloqueos en intercambios emparejados (§3.10).
- Documentación de CMake, módulo FindMPI: `MPIEXEC_EXECUTABLE`,
  `MPIEXEC_NUMPROC_FLAG`, `MPIEXEC_PREFLAGS/POSTFLAGS`,
  `MPI_CXX_LIBRARY_VERSION_STRING`.
- Documentación de Open MPI, `mpirun --oversubscribe`.
- Documento del equipo "Entregable S4" (R. Munayco): partición por franjas,
  halos superior e inferior y `MPI_Sendrecv` (ya citado en MPI_PROTOTYPE.md).

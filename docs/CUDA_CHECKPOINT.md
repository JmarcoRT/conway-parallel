# Checkpoint de feature/cuda

## Estado terminado

Base: `develop` en `9a847178caadadcfa69e298a40f120a09d690362`.
Esta rama contiene el backend CUDA y **la integración programática en Game**.
El checkpoint publicado de partida fue `7c815c56a87a3b06ff174ebf78a28834bf543571`;
este checkpoint reúne la integración Game, presets y presentación común. Incluye
selección CUDA FTXUI/textual, simulación normal con V y didáctico CUDA manual
con D. No incluye reproducción automática de exploración ni benchmarks CUDA.
Secuencial, OpenMP y MPI conservan sus controles.

- CMake opcional, `CONWAY_ENABLE_CUDA=OFF` por defecto: CPU no busca toolkit.
- `core::CudaBackend` movible y no copiable, RAII, buffers residentes, stream propio
  y snapshots `GameState` CPU por valor. Comparte B3/S23 sin runtime en el core común.
- Validación de entradas y tamaños, halos muertos, población y generación exactos,
  incluido el wrap de `size_t` que ya usa Game.
- Errores de lanzamiento/ejecución/transferencia explícitos. Backend invalidado
  tras fallo; limpieza de mejor esfuerzo si el contexto CUDA deja de ser utilizable.
- Pruebas exactas contra Game y el recorrido Secuencial, varios tamaños y generaciones,
  512 vecindarios, snapshots independientes, movimientos y quince fallos de API
  controlados. La prueba de fallos usa wrapping del linker solo en su ejecutable.
- Smoke de motor actualizado a confirmación `U`; lectura PTY del benchmark ajustada
  para eliminar esperas fijas después de recibir un burst. Aserciones y timeout de
  150 s conservados; ninguna adaptación de producción para hacer pasar esas pruebas.

Contratos detallados: [CUDA.md](CUDA.md). Todo el código y las pruebas necesarios
están en Git. No se necesitan los builds, logs ni documentos externos del equipo
de origen; se reconstruyen las dependencias y los ejecutables.

## Continuar en otro equipo

Para un clon nuevo:

```bash
git clone --branch feature/cuda https://github.com/JmarcoRT/conway-parallel.git
cd conway-parallel
```

En un clon existente, obtener la rama remota y cambiar a ella desde un árbol limpio.
No reutilizar cachés CMake, objetos o bibliotecas de otro equipo/distribución.

Entorno comprobado: Ubuntu 24.04/WSL2, GCC/G++ 12.4, CMake 3.28.3, Make, Git,
Python 3.12 y Open MPI 4.1.6 de Ubuntu. CMake requiere OpenMP y MPI incluso con
CUDA desactivado; obtiene FTXUI desde su commit fijado y necesita red en la primera
configuración. No instalar paquetes Python adicionales. El preset Release del
repositorio exige G++ 14: para este checkpoint usar las órdenes explícitas siguientes.

### CPU, sin requerir toolkit ni GPU

Desde la raíz del repositorio:

```bash
OMPI_CC=/usr/bin/gcc-12 OMPI_CXX=/usr/bin/g++-12 cmake -S . -B ../build-conway-cpu \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DMPI_CXX_COMPILER=/usr/bin/mpicxx -DMPIEXEC_EXECUTABLE=/usr/bin/mpiexec \
  -DPython3_EXECUTABLE=/usr/bin/python3 -DBUILD_TESTING=ON -DCONWAY_ENABLE_CUDA=OFF
cmake --build ../build-conway-cpu -j2
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir ../build-conway-cpu --output-on-failure
```

La suite completa incluye pruebas PTY de varios minutos; no mide rendimiento.
Para repetir solo las regresiones pertinentes de la revisión:

```bash
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir ../build-conway-cpu --output-on-failure \
  -R '^(rule_bs23_per_cell|conway_correctness|game_openmp_vs_sequential|step_trace|input_functional|input_dimension_regressions|mpi_evolution_1|mpi_evolution_4|engine_selection_smoke|ftxui_benchmark_pty)$'
```

### CUDA opcional

Requiere toolkit con nvcc y driver compatible; tener toolkit no implica tener GPU.
Se validó CUDA Toolkit 12.5.1 (nvcc 12.5.82), G++ 12 y C++20 en RTX 3050 (8.6).
La arquitectura 86 del ejemplo debe ajustarse si la GPU de destino es distinta.
Fijar compiladores en la primera configuración de un build nuevo:

```bash
OMPI_CC=/usr/bin/gcc-12 OMPI_CXX=/usr/bin/g++-12 cmake -S . -B ../build-conway-cuda \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DMPI_CXX_COMPILER=/usr/bin/mpicxx -DMPIEXEC_EXECUTABLE=/usr/bin/mpiexec \
  -DPython3_EXECUTABLE=/usr/bin/python3 -DBUILD_TESTING=ON -DCONWAY_ENABLE_CUDA=ON \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.5/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12 -DCONWAY_CUDA_ARCHITECTURES=86
cmake --build ../build-conway-cuda -j2
ctest --test-dir ../build-conway-cuda --output-on-failure \
  -R '^(cuda_.*|game_cuda_.*|rule_bs23_per_cell)$'
CUDA_VISIBLE_DEVICES=-1 ctest --test-dir ../build-conway-cuda \
  -R '^(cuda_|game_cuda_)' --output-on-failure
```

Con GPU visible, las comprobaciones seleccionadas deben pasar. Con GPU oculta,
las pruebas que requieren GPU pueden omitirse explícitamente; las de diagnóstico
deben aprobarse. El número depende del filtro y de esta versión del checkpoint.
Los errores de driver/runtime hacen fallar las pruebas; no se convierten en Skipped.
`cuda_failure_cleanup` requiere Linux y linker GNU compatible.

## Registro histórico: base verificable y pendientes

En el equipo de origen: todos los targets compilaron con CUDA ON/OFF; suite CPU
completa 60/60 antes de la revisión; después de las correcciones de revisión,
10/10 regresiones CPU/MPI y 5/5 CUDA/regla. Se volvió a comprobar la ausencia de GPU
y la configuración CPU con la búsqueda del toolkit prohibida y nvcc inexistente.
Los resultados históricos y sus alcances están en CUDA.md; los logs generados no
forman parte del checkpoint. Justo antes de publicarlo se repitieron las órdenes
selectivas anteriores: 10/10 CPU/MPI (40,52 s, benchmark PTY de 39,39 s) y 5/5
CUDA/regla (1,57 s), todas aprobadas. No se repitió la suite completa.

**Compute Sanitizer sigue pendiente**, por la interfaz de depuración WDDM del host
Windows en el entorno de origen. No se cambió el registro. Equivalencia funcional
y errores de API sintetizados no certifican ausencia de errores de memoria o
carreras. Tampoco se probó pérdida real de dispositivo, agotamiento real de VRAM o
hardware multi-GPU. No se instala ni actualiza ningún driver como parte del build.

Los apartados de etapas que siguen son históricos: sus menciones a cambios sin
commit/push, HEAD y totales corresponden a cada momento anterior a la publicación
autorizada. Para reanudar, usar el resumen y la sección final de publicación.

## Integración Game implementada (2026-10-29)

- `Engine::CUDA` solo programático. Caché opaca exclusiva, creación perezosa.
- Copias CPU independientes, sin runtime GPU; asignación fuerte y movimientos
  noexcept sin compartir recursos. Header con layout uniforme ON/OFF.
- Invalidación con edición, clear, pasos CPU y cambio de motor. No hay acceso
  mutable directo a buffers; se prueban ediciones mediante referencias Game&.
- Snapshot candidato completo antes de publicar; cualquier fallo descarta caché,
  conserva CPU confirmado y permite reconstrucción/reintento. Sin fallback.
- Sink CUDA no nulo rechazado antes de mutar; `setEngine` conserva noexcept.
- Pruebas mediante Game, independencia de copias/movimientos y recuperación ante
  fallos de API y `bad_alloc` CPU después de avanzar GPU.
- Arquitectura configurable: `CONWAY_CUDA_ARCHITECTURES=86` para RTX 3050;
  `75` para GTX 1650. La validación en hardware GTX 1650 sigue pendiente.

Los dos entregables siguen separados: CUDA en una laptop y clúster MPI entre
laptops con CPU. No se añadió selección FTXUI, benchmarks CUDA ni MPI+CUDA.
Compute Sanitizer, errores reales de dispositivo y validación MPI entre laptops
siguen pendientes. No se cambiaron Windows, drivers ni herramientas.

## Validación de la integración Game (2026-10-29)

Ubuntu 24.04.5/WSL2, RTX 3050 (8.6), nvcc 12.5.82, GCC/G++ 12.4.0.
Partida: rama `feature/cuda`, HEAD y origin
`7c815c56a87a3b06ff174ebf78a28834bf543571`, árbol limpio antes de trabajar.
Se consultó origin con `git ls-remote`, sin modificar referencias ni archivos.
Los cambios actuales no tienen commit ni push.

| Comprobación | Resultado |
|---|---|
| CUDA ON, arquitectura 86, todos los targets | Compilación aprobada |
| CUDA OFF por defecto, todos los targets | Compilación aprobada; búsqueda de toolkit prohibida y nvcc inexistente |
| CUDA/Game/regla en RTX 3050 | 7/7 aprobadas, 2,38 s |
| CUDA/Game con GPU oculta | 3 aprobadas, 3 Skipped, 0 fallos |
| Regresiones OFF | 14/14 aprobadas, 55,61 s |
| Regresiones ON | 17/17 aprobadas, 66,51 s |
| Arquitectura 75 | `game_cuda_tests` y backend compilados/enlazados; sin ejecución en GTX 1650 |
| Configuración uniforme | Los 19 targets que compilan Game en ON reciben CONWAY_HAS_CUDA=1; OFF no enlaza cudart |

Las regresiones OFF incluyen Game sin CUDA, regla, corrección, equivalencia OpenMP,
trazas, entrada/dimensiones, evolución MPI con 1 y 4 procesos, cliente/sesión MPI,
selección de motor y el benchmark PTY existente. ON repite esas regresiones CPU
(sin volver a contar los dos tests Game del filtro CUDA) y añade app_runtime,
app_runtime_thread_limit, didactic_session, didactic_mpi y didactic_layout.
En esa validación selectiva no se repitió la suite completa (véase la revisión
posterior más abajo). Los procesos MPI se ejecutaron en esta PC;
esto no valida todavía el clúster entre laptops.

Para reproducir las regresiones ON (en OFF añadir `game_cuda_.*` y omitir los
cinco tests de app/didáctica para reproducir exactamente las 14 seleccionadas):

```bash
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir ../build-conway-cuda --output-on-failure \
  -R '^(rule_bs23_per_cell|conway_correctness|game_openmp_vs_sequential|step_trace|input_functional|input_dimension_regressions|mpi_evolution_1|mpi_evolution_4|mpi_simulation|mpi_client|engine_selection_smoke|ftxui_benchmark_pty|app_runtime|app_runtime_thread_limit|didactic_session|didactic_mpi|didactic_layout)$'
```

Pendientes: ejecución y equivalencia en GTX 1650 con arquitectura 75, Compute
Sanitizer y validación del clúster CPU/MPI entre laptops. No se modificaron
Windows, drivers ni herramientas. No se añadieron FTXUI, benchmarks CUDA ni MPI+CUDA.

## Revisión del diff y suites completas (2026-10-29)

Se revisó la integración respecto a HEAD
`7c815c56a87a3b06ff174ebf78a28834bf543571` en `feature/cuda`, conservando
los cambios de la etapa anterior. No se encontraron defectos de producción
concretos que justifiquen modificar el código. Esta revisión solo actualiza
el checkpoint; no añade funcionalidades, commits ni push.

Comprobaciones del diff:

- No se exponen referencias mutables a GameState ni a vectores. Una referencia
  Game& retenida usa setCell/clear/asignación, que invalidan o reemplazan la caché.
  Una edición sin cambio o fuera de rango no desactualiza el estado GPU.
- Copiar incluye ambos buffers y contadores CPU, conserva el motor y deja la
  caché vacía. La asignación prepara un candidato antes de intercambiar. Los
  movimientos transfieren propiedad exclusiva sin reservar, con noexcept y
  static_assert sobre los tipos internos. El origen admite destrucción/asignación.
- stepCuda no escribe state_ durante creación de caché, avance GPU ni snapshot;
  publica el candidato completo mediante movimiento noexcept. Su catch también
  cubre bad_alloc del snapshot después de avanzar GPU y descarta la caché.
- El reintento reconstruye desde current y generation confirmados. El siguiente
  kernel escribe todo el interior y el swap deja el current anterior como next,
  igual que el motor secuencial. No reutiliza una GPU adelantada.
- OFF: 17 unidades Game sin macro CUDA, cero compilaciones CUDA y ninguno de los
  40 ejecutables ELF inspeccionados enlaza libcuda/cudart. ON: 19 unidades Game
  con configuración CUDA uniforme. Se conservó la configuración OFF que prohíbe
  buscar toolkit y apunta nvcc a una ruta inexistente.

Como verificación auxiliar se compilaron copias temporales de los tests Game
CUDA con GCC `-fno-access-control`, sin cambiar headers ni API de producción.
Se compararon current y next completos (halos incluidos), stride, dimensiones
y contadores. Ambos verificadores pasaron en RTX 3050, incluidos errores de API,
bad_alloc CPU tras avance GPU, reconstrucción/reintento y fallo de asignación por
copia. No son pruebas CTest versionadas; las pruebas públicas Game no inspeccionan
directamente next. Esta comprobación auxiliar no equivale a Compute Sanitizer.

Las suites completas OFF y ON se ejecutaron secuencialmente, sin filtros y con
los timeouts/aserciones originales, después de compilar todos los targets de
ambos builds. Se contrastaron la salida CTest y los informes JUnit. No se cambió
código de producción ni pruebas durante estas ejecuciones.

| Suite completa | Aprobadas | Fallos | Omitidas/Skipped | Deshabilitadas | Duración |
|---|---:|---:|---:|---:|---:|
| CUDA OFF | 62/62 | 0 | 0 | 0 | 815,10 s |
| CUDA ON, arquitectura 86, RTX 3050 | 66/66 | 0 | 0 | 0 | 816,30 s |

En OFF los dos tests Game CUDA verifican rechazo explícito de CUDA y recuperación
CPU; no ejecutan equivalencia GPU. En ON se ejecutó realmente la equivalencia
CUDA y la inyección de errores, sin omisión por falta de GPU. El test de ausencia
oculta la GPU solo en su propio proceso y pasó. Los tres Skipped del ensayo
anterior con toda la GPU oculta pertenecen a esa ejecución selectiva histórica,
no a estas suites completas.

Comandos de esta revisión (builds locales fuera del repositorio):

```bash
cmake --build /home/jean/cuda-setup/build/game-off -j2
cmake --build /home/jean/cuda-setup/build/game-on -j2
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-off --output-on-failure \
  --output-junit /home/jean/cuda-setup/logs/review-full-off.xml
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-on --output-on-failure \
  --output-junit /home/jean/cuda-setup/logs/review-full-on.xml
```

Los builds usan fuentes del repositorio; reutilizan únicamente la descarga local
de FTXUI. Para reconstruir en otro equipo, usar las configuraciones portables de
las secciones anteriores. Los logs/JUnit locales no son requisitos del checkpoint.

Limitaciones y pendientes: estas ejecuciones validan RTX 3050 (86), no hardware
GTX 1650. Para 75 solo se conserva la compilación/enlace aprobada de la etapa
anterior; todavía falta ejecución en GTX 1650. Compute Sanitizer sigue pendiente,
sin certificación de memoria/carreras. Los fallos de API y bad_alloc son
sintetizados; no se probó pérdida real de dispositivo, agotamiento real de VRAM ni
multi-GPU. MPI se probó en esta misma PC, no entre laptops. No se modificaron
Windows, drivers o herramientas; no se añadió FTXUI, benchmarks CUDA ni MPI+CUDA.

Estado Git al cerrar la revisión: rama feature/cuda, HEAD conservado en
7c815c56a87a3b06ff174ebf78a28834bf543571; ocho archivos versionados modificados
y tests/game_cuda_tests.cpp sin seguimiento. El índice permanece sin cambios;
no se hicieron commits ni push. git diff --check no reporta errores.

## Etapa de configuración programática de bloques (2026-10-29)

La geometría anterior era lineal, con 256 hilos y grid-stride limitado a 1024
bloques. Ahora se usa un mapeo 2D real, x=columna/y=fila, una posición por hilo,
sin truncar la cuadrícula. Presets: 8×8 (64 hilos), 16×16 y 32×8 (256 hilos).
El predeterminado explícito es 16×16; z=1. Estos números describen el lanzamiento,
no la cantidad de hilos simultáneos ni ocupación/rendimiento.

API documentada en CUDA.md: Game::setCudaBlockSize, cudaBlockSize y
cudaLaunchConfiguration; CudaBackend::setBlockSize y launchConfiguration.
La biblioteca CPU conway_cuda_launch ofrece planCudaLaunch sin toolkit/GPU.
La consulta efectiva comprueba límites reales del dispositivo/kernel y no reserva
los tableros GPU. Se rechazan bloques no soportados, grid que excede los límites
y desbordamientos, sin fallback ni recorte.

Un cambio válido conserva CPU/GPU buffers, halos, población, generación y stream;
solo publica la geometría candidata. Un rechazo/error conserva también la
geometría anterior y la caché. Edición/clear/cambio de motor conservan la selección
al reconstruir la caché. Copias y movimientos conservan la selección sin compartir
propietarios GPU. Configurar/consultar geometría efectiva requiere ON y GPU;
OFF admite consulta de selección y planificación CPU pura, y rechaza las otras
operaciones explícitamente.

Las garantías antes cubiertas solo por verificadores temporales pasan a CTest:
game_cuda_contract y cuda_failure_cleanup comparan current/next completos,
halos, stride, dimensiones y contadores mediante un acceso const de test, sin
-fno-access-control ni macros que cambien el layout de Game. La inyección de
bad_alloc cubre ambas reservas de snapshot tras avanzar GPU. Se prueban todos
los presets contra CPU y sus 512 vecindarios, tableros pequeños/irregulares,
referencias retenidas, cambios entre generaciones, copia/movimiento y caché.
La nueva cuda_launch_geometry es CPU pura y funciona OFF/sin GPU.

Los tests de límites combinan propiedades reales (incluidos bordes máximos de
cuadrícula x/y, sin reservar tableros gigantes) con límites reducidos sintéticos
para probar cada rechazo de bloque/cuadrícula y el máximo de hilos del kernel.
Se comprueba que cambiar geometría no reserva ni libera recursos GPU residentes.

Se detectó y corrigió durante la compilación una colisión del namespace input
en una prueba MPI al incluir validación de dimensiones desde el header público.
La validación reside ahora en la biblioteca CPU independiente; no se cambió la
prueba MPI ni su comportamiento para evitar el conflicto.

Resultados de esta versión, después de compilar todos los targets ON/OFF y de
confirmar los informes JUnit, sin cambiar código ni tests durante las suites:

| Comprobación | Aprobadas | Fallos | Skipped | Duración/alcance |
|---|---:|---:|---:|---|
| Suite completa CUDA OFF | 63/63 | 0 | 0 | 809,00 s |
| Suite completa CUDA ON, RTX 3050/86 | 67/67 | 0 | 0 | 811,85 s |
| Selectivas CUDA/Game/regla | 8/8 | 0 | 0 | 2,91 s |
| Selectivas con GPU oculta | 4 | 0 | 3 | Geometría/validación/ausencia aprobadas; tres tests que requieren GPU omitidos |
| Arquitectura 75 | No ejecutada | — | — | Backend y game_cuda_tests compilan y enlazan; no prueba hardware GTX 1650 |

Ambas suites completas tuvieron cero tests deshabilitados. OFF comprueba la
planificación CPU pura y el rechazo explícito de la API efectiva CUDA; no prueba
el kernel. ON sí ejecuta los tres presets en la RTX 3050. Las pruebas ocultas que
se omiten son cuda_failure_cleanup, cuda_equivalence y game_cuda_contract;
no se cuentan como aprobaciones.

OFF conserva CONWAY_ENABLE_CUDA=OFF por defecto, búsqueda de toolkit prohibida y
nvcc inexistente en la configuración de control. Se inspeccionaron 41 ejecutables
ELF: ninguno enlaza libcuda/cudart. No hay compilaciones .cu ni macro CUDA en OFF;
las 17 unidades Game usan la configuración CPU. Las 19 unidades Game de ON usan
la misma configuración opcional del backend.

Las suites completas se ejecutaron secuencialmente, con el entorno de la sección
anterior, usando estos comandos y conservando todos los timeouts originales:

```bash
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-off --output-on-failure \
  --output-junit /home/jean/cuda-setup/logs/block-full-off.xml
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-on --output-on-failure \
  --output-junit /home/jean/cuda-setup/logs/block-full-on.xml
```

Pendientes/limitaciones: GTX 1650 solo tiene comprobación de compilación para 75,
no validación en dispositivo. Compute Sanitizer sigue pendiente: equivalencia y
fallos sintetizados no certifican memoria o carreras. Consultar una cuadrícula
en el límite no implica haber lanzado esa cuadrícula ni disponer de VRAM para
sus tableros. No se midió rendimiento, ocupación o simultaneidad de hilos. No se
probó pérdida real de dispositivo, agotamiento real de VRAM ni multi-GPU. MPI se
validó en esta PC; el clúster entre laptops sigue pendiente. No se añadieron
FTXUI, modo didáctico, benchmarks CUDA ni MPI+CUDA; no se cambiaron herramientas,
drivers ni Windows.

Estado Git de esta etapa: feature/cuda, HEAD
7c815c56a87a3b06ff174ebf78a28834bf543571 sin cambios, índice intacto, nueve archivos
versionados modificados y cinco sin seguimiento. git diff --check limpio.
Sin commits ni push. Los resultados completos y totales Git anteriores son
históricos de las revisiones previas del kernel.

## Etapa UI: selección CUDA y simulación normal (2026-10-29)

Se conservaron los seis elementos de navegación del menú, las teclas existentes,
la entrada inicial inmutable del catálogo y sus controles Secuenciales. V abre
la simulación normal reutilizando Game, SimulationSession y el renderizador de
tableros; no incorpora CUDA en DidacticController ni en los benchmarks.

- Selección M → G con disponibilidad/diagnóstico guardado: OFF, GPU ausente,
  fallo de inicialización o GPU disponible. Prueba de inicialización 1×1 sin
  evolución; ninguna consulta CUDA se hace desde renderizado.
- Bloques 8×8, 16×16 predeterminado y 32×8 mediante recursos, solo para CUDA.
  OpenMP conserva hilos y MPI procesos, independientemente del bloque CUDA.
- Ajustes pausan y confirman sin reiniciar el estado. La sesión normal y su
  configuración se conservan al volver al menú; V no reinicializa la GPU de
  una sesión ya retenida. R reinicia y confirmar otro patrón reemplaza la sesión.
- Snapshot CPU confirmado por CUDA; sin paso especulativo NEXT ni trazas CUDA.
  Motor/GPU/bloque/hilos por bloque/grid son información de lanzamiento, no
  simultaneidad ni ocupación. La geometría se separa en dos líneas a 40×18.
- Error visible, sesión pausada y último estado confirmado retenido. U recupera,
  sin fallback. Se probaron errores de inicialización, lanzamiento y configuración
  asíncrona, y bad_alloc al enviar configuración, incluidos ambos buffers/halos.
- Recuperar una transición MPI→CUDA fallida fija CUDA explícitamente; no permite
  conservar accidentalmente un Game CPU bajo una sesión marcada CUDA. Son cambios
  de motor sucesivos, con cierre del trabajador MPI antes del motor CUDA local.

Pruebas permanentes nuevas: cuda_ui_session, cuda_ui_unavailable, cuda_ui_flows
y (ON) cuda_ui_absent_flows. Los flujos ejecutan la aplicación real en modo textual
y PTY, con selección, Enter/atajos, cancelación, bloques, pausa/reproducción,
reinicio, salida/reentrada, 120×42 y 40×18; el oráculo de sesión compara ambos
buffers con CPU. cuda_failure_cleanup amplía la inyección de errores de UI/sesión.

Incidencias detectadas y corregidas: pérdida de la etiqueta Q salir en ayuda
compacta (se restauró sin modificar aserciones) y recorte de los valores de grid
a 40×18 (se separó la geometría y se añadió aserción numérica permanente).
Una primera ejecución completa se interrumpió deliberadamente para corregir el
envío transaccional; no se cuenta como una suite completa aprobada ni fallida.
Resultados finales de la versión corregida, compilando todos los targets de
ambos builds antes de ejecutar CTest secuencialmente, sin filtros y con los
timeouts originales. Se contrastaron la salida y los informes JUnit:

| Suite completa final | Aprobadas | Fallos | Skipped | Deshabilitadas | Duración |
|---|---:|---:|---:|---:|---:|
| CUDA OFF | 66/66 | 0 | 0 | 0 | 818,38 s |
| CUDA ON, RTX 3050 / arquitectura 86 | 71/71 | 0 | 0 | 0 | 838,33 s |

La única regresión funcional detectada por los controles fue la etiqueta Q salir;
pasó después de restaurarla y volvió a pasar en ambas suites completas. La
comprobación de cuadrícula compacta también pasó en la suite final. La ejecución
interrumpida y las primeras pruebas con fallo no se suman a estas aprobaciones.

ON ejecutó CUDA realmente; cuda_failure_cleanup verificó inicialización fallida,
fallo de lanzamiento, bad_alloc del mensaje de configuración y recuperación
MPI→CUDA sin paso CPU silencioso. cuda_ui_session comparó los buffers con CPU.
cuda_ui_flows verificó la aplicación real textual/PTY en 120×42 y 40×18;
cuda_ui_absent_flows verificó rechazo con GPU oculta, como aprobación del manejo
de ausencia, no como equivalencia GPU. En OFF los tests CUDA de UI comprueban
rechazo por build sin CUDA y conservación de controles/estado CPU.

Aislamiento OFF: 42 ejecutables ELF inspeccionados sin libcuda/cudart; ninguna
compilación .cu ni macro CUDA. Sus 18 unidades Game usan configuración CPU;
las 20 unidades Game de ON usan la configuración CUDA uniforme. La configuración
OFF conserva búsqueda de toolkit prohibida y nvcc inexistente. El trabajador MPI
continúa CPU y no incorpora Game/CUDA.

Reproducción exacta de las suites de esta etapa:

```bash
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-off --output-on-failure \
  --output-junit /home/jean/cuda-setup/logs/ui-full-final-off.xml
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-on --output-on-failure \
  --output-junit /home/jean/cuda-setup/logs/ui-full-final-on.xml
```

Se mantuvo compilación/enlace del backend y game_cuda_tests con arquitectura 75;
la aplicación y el hardware GTX 1650 no se validaron allí. No se lanzó Compute
Sanitizer, ni se provocó pérdida real de dispositivo, agotamiento real de VRAM o
multi-GPU. Los fallos de inicialización/lanzamiento/memoria son sintetizados.
MPI se ejecutó en esta PC, no entre laptops. La simulación CUDA normal descarga
un snapshot CPU completo por paso y es síncrona; no certifica rendimiento ni
respuesta interactiva bajo tableros arbitrariamente grandes.

Git de cierre: rama feature/cuda, HEAD
7c815c56a87a3b06ff174ebf78a28834bf543571 conservado, índice intacto;
14 archivos versionados modificados y 9 sin seguimiento (incluyen etapas previas).
git diff --check limpio; sin commits ni push.

Comandos manuales exactos y controles en CUDA.md, sección
«CUDA en selección y simulación normal». La persistencia es dentro de la misma
aplicación; no se guarda una sesión en disco. GTX 1650 y Compute Sanitizer siguen
pendientes; no se midió rendimiento. No se cambiaron herramientas, drivers ni
Windows y no se hicieron commits ni push.

## Etapa didáctica CUDA manual aprobada (2026-10-29)

Implementación sin cambiar el kernel ni los contratos de Game/SimulationSession.
Se reutilizan la sesión normal y las primitivas de célula/borde. Nuevos archivos:
app/cuda_didactic_session y ui/cuda_didactic_view (headers y fuentes). Modelo
pequeño sin tablero ni mapas: índices base cero, geometría planificada CPU y
CellEvaluation calculado con el evaluador compartido sobre un stencil de nueve
valores como máximo. El lector no se invoca si el centro es exterior. Las
fronteras entre bloques no son bordes muertos.

D pausa la reproducción normal y descarta ticks pendientes. HJKL, [/] y N solo
exploran; G es el único avance CUDA real. No hay autoplay de selección ni velocidad.
B cambia preset con confirmación/cancelación y conserva coordenada global o la
ajusta si deja de caber en la cobertura. R reinicia la entrada inicial y selección,
conservando preset; menú/V/D comparten el mismo estado confirmado. U recupera sin
avanzar. Durante error permanece el frame/buffers/generación confirmados, sin
fallback. El avance sigue siendo síncrono, como la simulación normal.

Aviso siempre visible: «Exploración conceptual; no representa el orden de ejecución
GPU». Predicción explícitamente CPU B3/S23 compartida, distinta de G CUDA real.
El panel exterior corresponde al kernel inspeccionado: no lee/escribe célula,
aporta cero y alcanza todas las barreras/reducción del bloque. No se infiere
cronología, simultaneidad ni ocupación. Render consume referencias const y la
caché pequeña: sin copias de Game/tablero, consultas CUDA ni mapas completos.
La cobertura exterior se dibuja virtualmente solo en el viewport visible.

Pruebas nuevas: cuda_didactic_model, cuda_didactic_integration,
cuda_didactic_pty y (ON) cuda_didactic_absent_pty. El modelo verifica 35×19, sus
871 exteriores con 16×16, 512 vecindarios y vecinos entre bloques. La integración
prueba todos los bloques/hilos de los tres presets, equivalencia de buffers con
CPU, predicción frente a G, remapeo/reinicio y frame estable al renderizar. El
wrapper de fallos incorpora generación fallida por lanzamiento y por cada reserva
de snapshot, recuperación, y contadores que detectan cualquier consulta de
conteo/dispositivo/propiedades/atributos o reserva GPU durante exploración/render.
PTY usa la aplicación real, 120×42 y 40×18: pausa al entrar, G, manual sin avance,
exteriores, presets/cancelación, persistencia, reinicio y resize bajo mínimo/restaurado.

Una primera prueba de resize conservaba filas del raster anterior en el lector;
se corrigió descartando ese raster, igual que los tests existentes, sin eliminar
las aserciones de tamaño/contenido. El layout compacto reserva espacio para el
tablero incluso tras confirmar preset/remapear o mostrar error; V amplía detalle.

No se repiten las suites completas anteriores (66/66 OFF, 71/71 ON): se mantienen
como base histórica. Se ejecutan pruebas específicas y regresiones directamente
afectadas de didáctico CPU/MPI, primitivas, normal, selección, catálogo y layout.
Resultados finales comprobados en salida CTest y JUnit, compilando todos los
targets ON/OFF y manteniendo timeouts/aserciones existentes:

| Selección de esta etapa | Aprobadas | Fallos | Skipped | Duración |
|---|---:|---:|---:|---:|
| ON, RTX 3050/86: específicas + regresiones afectadas | 20/20 | 0 | 0 | 268,41 s |
| OFF: modelo/ausencia + controles afectados | 9/9 | 0 | 0 | 48,62 s |
| Integración GPU OFF, comprobación aparte | No ejecutada | 0 | 1 | Omitida explícitamente, no equivale a aprobación GPU |

No hubo tests deshabilitados en las selecciones finales. El fallo inicial del
lector PTY se corrigió y se repitió; no se cuenta como aprobación. La prueba PTY
final cubre también cambio de preset/remapeo a 40×18 sin perder tablero, aviso
conceptual ni detalles esenciales. Se probaron las 4032 posiciones de lanzamiento
entre los tres presets en 35×19, con 665 válidas en cada preset y 295/871/871
exteriores para 8×8/16×16/32×8 respectivamente.

Las regresiones ON incluyen regla, selección, didáctico CPU/MPI, primitivas,
controladores/vistas PTY CPU/MPI, catálogo textual/PTY, encabezado/menú, layout,
sesión normal y disponibilidad ON/ausente, además de todas las nuevas específicas.
OFF incluye el didáctico CPU, layout/primitivas, selección, normal, modelo puro y
rechazo del nuevo modo sin backend. No se ejecutó la suite completa de esta versión:
las 66/66 OFF y 71/71 ON anteriores permanecen históricas, como se pidió.

```bash
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-on --output-on-failure \
  -R '^(cuda_failure_cleanup|cuda_didactic_.*|didactic_session|didactic_layout|didactic_mpi|ftxui_didactic_controller_pty|ftxui_didactic_mpi_views_pty|cuda_ui_session|cuda_ui_flows|cuda_ui_absent_flows|engine_selection_smoke|ftxui_header_pty|ftxui_menu_pty|responsive_layout|catalog_preview_pty|text_catalog_preview|rule_bs23_per_cell)$' \
  --output-junit /home/jean/cuda-setup/logs/did-final-on.xml
env -u NO_COLOR OMPI_CXX=/usr/bin/g++-12 PYTHONDONTWRITEBYTECODE=1 \
  ctest --test-dir /home/jean/cuda-setup/build/game-off --output-on-failure \
  -R '^(cuda_didactic_model|cuda_didactic_pty|didactic_session|didactic_layout|engine_selection_smoke|cuda_ui_session|cuda_ui_flows|responsive_layout|ftxui_didactic_controller_pty)$' \
  --output-junit /home/jean/cuda-setup/logs/did-final-off.xml
```

Aislamiento final OFF: 44 ejecutables ELF sin libcuda/cudart, cero compilaciones
.cu ni macro CUDA; 20 unidades Game CPU. ON: 22 unidades Game con macro uniforme.
No se modificó el kernel ni se repitió la compilación 75 en esta etapa: el modo
se validó solo en RTX 3050, no en GTX 1650. No se probó memoria/carreras con
Compute Sanitizer, ni pérdida real de dispositivo/VRAM; los errores son inyectados.
Los pasos G son síncronos y pueden demorar la UI en tableros grandes. No hay modo
didáctico textual ni reproducción automática de selección/controles de velocidad.

Git final: feature/cuda, HEAD 7c815c56a87a3b06ff174ebf78a28834bf543571 conservado,
índice intacto, 14 archivos versionados modificados y 16 sin seguimiento (incluyen
etapas previas). git diff --check limpio. Comandos manuales con el ejemplo
35×19 en CUDA.md. GTX 1650 y Compute Sanitizer permanecen pendientes; sin cambios
de herramientas, drivers o Windows; sin commits ni push.

## Presentación común del didáctico CUDA (2026-10-29)

Cambios de esta etapa, sobre el árbol de trabajo anterior: extracción de
`include/ui/board_renderer.hpp` y `src/ui/board_renderer.cpp`; los paneles
CURRENT/NEXT existentes y CUDA comparten geometría y rejilla. Las decoraciones
son opcionales. Se conserva la salida anterior sin decoraciones mediante cuatro
capturas de referencia tomadas antes del cambio (80×24 y 40×12, color/monocromo).
CUDA conserva las primitivas de célula, el verde y el espaciado originales.
@1/@0 sobre ámbar marca selección viva/muerta; :1/:0 sobre azul marca vecinos;
ambos caben centrados en anchos de célula de 2 a 8. Sin color, selección y contorno
invertidos, vecinos subrayados y marcadores explícitos. La leyenda usa las mismas
primitivas. Se corrigió durante revisión que el decorador gris base ocultaba el
cian del contorno; queda una regresión permanente del color efectivo.

El rectángulo visible contiene solo células reales: 5×5 no se rellena hasta
16×16. Línea gruesa para límites reales de bloque; fina para fin de tablero que
corta un bloque; discontinua para recorte del viewport. Un límite real que
coincide con recorte se conserva como dato y se dibuja discontinuo. Solo se
resalta el perímetro del bloque seleccionado, sin rellenar sus células. Un hilo
exterior no desplaza el viewport hacia una célula ficticia.

A alterna cobertura secundaria acotada: dimensiones y cantidad exterior, más
mapa de bloques pequeño en detalle; en compacto, A:+cantidad y V para detalle.
No mueve, amplía ni cambia los píxeles del tablero. Estado conservado al salir y
volver, y al cambiar preset. Permanecen el aviso conceptual, predicción CPU con
regla compartida y G como avance CUDA real. No cambia cálculo, kernel, buffers,
contadores ni sesión; no hay consultas CUDA, copias completas ni mapas de tablero
por renderizado. Las firmas de callbacks de los tests de integración/fallos se
adaptaron al ancho de célula; no se ejecutaron sus pruebas del backend esta etapa.

### Verificación acotada de esta etapa

Se compilaron aplicación, fixture didáctico, renderer y layout con ON/OFF. CTest:

```bash
ctest --test-dir /home/jean/cuda-setup/build/game-off \
  -R '^(board_renderer_layout|didactic_layout|cuda_board_visual_pty|ftxui_didactic_controller_pty|ftxui_didactic_mpi_views_pty)$' \
  --output-on-failure
ctest --test-dir /home/jean/cuda-setup/build/game-on \
  -R '^(board_renderer_layout|didactic_layout|cuda_board_visual_pty|ftxui_didactic_controller_pty|ftxui_didactic_mpi_views_pty)$' \
  --output-on-failure
```

- OFF: **5/5, 97,39 s**. ON/RTX 3050 (arquitectura 86): **5/5, 131,84 s**.
  Cero fallos, omisiones o deshabilitadas en estas ejecuciones finales. En OFF la
  PTY CUDA comprueba rechazo con diagnóstico, no valida ejecución en GPU.
- `board_renderer_layout`: 5×5 y 35×19, tres presets, selección viva/muerta/
  exterior, color/monocromo y tres áreas (180×55, 120×32, 40×10): 216 renders
  contando A OFF/ON. Comprueba marcadores/atributos/anchos, procedencia de límites,
  extensión real, contorno visible y píxeles/viewport invariables al activar A.
- `didactic_layout`: referencias del renderer previo, además de sus aserciones
  existentes. PTY heredadas: controles Secuencial/OpenMP y vistas MPI del tablero
  compartido. PTY nueva: ambos tableros, color/NO_COLOR, presets, A/V, persistencia,
  diagnóstico exterior y resize 40×18; explorar no cambia G0.
- Inspección visual efectuada sobre PNG de los rasters FTXUI y capturas ANSI de
  PTY con Cairo y fuente monoespaciada instalada: 5×5/35×19, selección viva,
  muerta/exterior, color/monocromo, cobertura y terminal pequeña. Incluye revisión
  de los tres presets. Son reconstrucciones de las celdas/atributos emitidos,
  **no capturas del escritorio de Windows Terminal**; su fuente/paleta final
  requiere revisión manual del usuario. Artefactos locales en
  `/home/jean/cuda-setup/visual-review/` (no son necesarios para compilar).
- Los intentos iniciales dentro del sandbox fallaron al inicializar CUDA
  (`cudaErrorInsufficientDriver`) o abrir sockets MPI (`listen failed`). No se
  contaron como aprobaciones: se repitió la selección con acceso GPU/sockets y
  pasó. No se tocaron herramientas ni drivers. `ldd` del ejecutable OFF no tiene
  libcuda/cudart; `git diff --check` limpio.

No se repitieron suites completas ni pruebas del backend CUDA. No hay commits
ni push. GTX 1650/arquitectura 75 y Compute Sanitizer siguen pendientes. El tamaño
mínimo admitido por la aplicación sigue siendo 40×18: debajo se pide ampliar la
terminal. En una terminal pequeña solo se muestra un viewport y V da el detalle;
activar A no consume filas adicionales del tablero.

## Checkpoint para reanudar: publicación autorizada (2026-10-29)

Rama de trabajo y destino: `feature/cuda` → `origin/feature/cuda`. Este commit
reúne las etapas anteriores completas: integración Game, presets CUDA,
selección FTXUI/textual, simulación normal, didáctico manual y renderer compartido,
con pruebas y documentación. CUDA permanece opcional y OFF por defecto. MPI
permanece CPU: no hay MPI+CUDA, benchmark CUDA ni exploración automática.

La revisión final corrigió la normalización Git de los fixtures: `.gitattributes`
conserva sus bytes CRLF y padding exactos. Se verificó igualdad de los cuatro
fixtures contra el índice y `didactic_layout_tests` pasó OFF/ON leyendo esos
bytes materializados en un directorio temporal. No quedan defectos conocidos pendientes
de corregir en el alcance de esta etapa.
La presentación aprobada está terminada. Última comprobación selectiva antes de
publicar: `didactic_layout`, `board_renderer_layout`, `cuda_board_visual_pty`:
**OFF 3/3, 3,48 s; ON/RTX 3050 3/3, 37,72 s**, sin fallos ni omisiones. Los
callbacks adaptados de `cuda_failure_tests` y `cuda_didactic_integration_tests`
compilaron/enlazaron con ON; sus pruebas GPU no se repitieron esta vez. Se reutilizan
los resultados previos de esta misma implementación: presentación/regresiones
compartidas OFF 5/5 y ON 5/5 de la sección anterior; suites completas anteriores
son históricas, no una ejecución completa de la última presentación.

Repetir solo las comprobaciones de cierre:

```bash
ctest --test-dir /home/jean/cuda-setup/build/game-off \
  -R '^(board_renderer_layout|didactic_layout|cuda_board_visual_pty)$' --output-on-failure
ctest --test-dir /home/jean/cuda-setup/build/game-on \
  -R '^(board_renderer_layout|didactic_layout|cuda_board_visual_pty)$' --output-on-failure
```

Abrir en esta PC (RTX 3050, build existente con arquitectura 86):

```bash
cd /home/jean/projects/conway-parallel
cmake --build /home/jean/cuda-setup/build/game-on --target conway -j2
env -u CUDA_VISIBLE_DEVICES -u NO_COLOR TERM=xterm-256color COLORTERM=truecolor \
  /home/jean/cuda-setup/build/game-on/conway
```

C → M → ruta del patrón → Enter → U; M → G selecciona CUDA; R → 2 confirma
16×16 (predeterminado); D abre el didáctico, V la simulación normal. En el
didáctico: HJKL hilo, [/] bloque, A cobertura compacta, V detalle, B preset,
G generación CUDA real, R reinicia, U recupera y Esc menú. Para probar sin color,
reemplazar `-u NO_COLOR` por `NO_COLOR=1` después de las opciones de `env`.
El ejemplo 35×19 y la preparación de un build nuevo están descritos arriba y
en CUDA.md. Ningún build ni artefacto local es necesario en el repositorio.

Pendientes explícitos para mañana:

- **GTX 1650/arquitectura 75:** falta ejecución en ese hardware; compilar para 75
  en la RTX 3050 no es validación funcional en GTX 1650.
- **Compute Sanitizer:** sigue pendiente; equivalencia y fallos inyectados no
  certifican memoria ni carreras. Sin cambios de Windows, drivers o herramientas.
- **Revisión visual directa de Windows Terminal:** se inspeccionaron rasters
  FTXUI/PTY reconstruidos, no capturas de su escritorio. La fuente, paleta y
  unión/peso de glifos de esa terminal requieren confirmación manual del usuario.
  No queda un fallo visual conocido sin corregir; ese alcance sigue sin verificar.
- Pérdida real de dispositivo/VRAM, multi-GPU y clúster MPI físico entre laptops
  siguen sin validación. G usa el paso síncrono existente y puede demorar la UI.

Publicación mediante commit y push normales, sin merge ni force push. Solo fuentes,
pruebas (incluidas cuatro capturas ANSI de referencia) y documentación; sin builds,
binarios, temporales ni secretos. Identificar el hash de este checkpoint con
`git rev-parse HEAD`; contrastar con `git ls-remote origin refs/heads/feature/cuda`.
Las notas anteriores de «sin commits ni push» quedan como registro histórico.

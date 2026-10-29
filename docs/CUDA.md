# CUDA: backend residente e integración programática en Game

`Engine::CUDA` selecciona el motor local GPU mediante `Game::setEngine`.
CUDA sigue siendo opcional (`CONWAY_ENABLE_CUDA=OFF`) y el motor predeterminado
sigue siendo Sequential. Hay selección CUDA en FTXUI/textual y simulación normal
con V. Hay modo didáctico CUDA manual; no hay benchmarks CUDA ni integración
MPI+CUDA; MPI continúa en su trabajador CPU externo.

## Contrato de Game

```cpp
conway::Game game(input);
game.setEngine(conway::Engine::CUDA);
game.step(); // Excepción explícita si CUDA no está compilado o disponible.
```

Game conserva el estado CPU confirmado, con sus dos buffers y contadores. Su
caché privada y opaca posee exclusivamente un backend GPU y se crea de forma
perezosa. Un paso obtiene un snapshot candidato completo y solo después publica
ambos buffers, población y generación mediante un movimiento que no lanza.
Cualquier excepción, incluido `bad_alloc` del snapshot tras avanzar la GPU,
descarta la caché y conserva el último estado CPU confirmado. El siguiente intento
reconstruye desde ese estado; nunca hay fallback silencioso.

Copiar Game copia ambos buffers CPU y el motor, sin consultar ni reservar GPU.
La asignación por copia prepara el candidato antes de reemplazar el destino.
Los movimientos transfieren la propiedad exclusiva y son noexcept; el origen
movido admite destrucción o nueva asignación. No se comparten recursos GPU.
Las autoasignaciones conservan el objeto.

`setCell` invalida la caché cuando cambia una célula; una edición sin cambio o
fuera de rango conserva el estado. `clear`, cambios de motor y pasos CPU descartan
la caché. Esto se cumple también mediante referencias `Game&` retenidas: Game no
expone referencias mutables a GameState ni a sus buffers. `setEngine` sigue siendo
noexcept. El sink de traza CUDA no nulo se rechaza antes de avanzar o modificar
estado; nullptr equivale a `step()`.

Todos los targets que compilan Game enlazan la misma configuración opcional del
backend. El layout del header no depende de macros CUDA. Con ON, los ejecutables
que usan Game enlazan cudart aunque seleccionen CPU; con OFF no existe esa
dependencia ni se busca el toolkit.

## Configuración programática de bloques 2D

```cpp
using namespace conway::core;
conway::Game game(input);
game.setCudaBlockSize(kCudaBlock32x8); // Valida dispositivo/kernel; no reserva tableros GPU.
game.setEngine(conway::Engine::CUDA);
game.step();
auto effective = game.cudaLaunchConfiguration();
```

Se aceptan únicamente `kCudaBlock8x8`, `kCudaBlock16x16` y `kCudaBlock32x8`
(también sus pares `{x,y}`). El predeterminado explícito es `kDefaultCudaBlock`,
16×16. Z es siempre 1 para bloque y cuadrícula. Los presets tienen 64, 256 y
256 hilos por bloque respectivamente. Esto no indica hilos ejecutándose
simultáneamente: no es una medición de ocupación, residencia ni rendimiento.

`cudaBlockSize()` devuelve la selección almacenada sin runtime y noexcept.
`cudaLaunchConfiguration()` devuelve bloque, threadsPerBlock, gridX/gridY,
launchedPositions y outsidePositions. La cuadrícula es
`ceil(width/block.x) × ceil(height/block.y)`; las posiciones lanzadas son su
producto por los hilos por bloque y las exteriores son ese total menos
`width*height`. Se cuentan posiciones lógicas externas, no halos ni accesos fuera
de memoria. Por ejemplo, 37×53 con 16×16 usa cuadrícula 3×4: 3072 posiciones,
1111 exteriores y 256 hilos por bloque.

La consulta efectiva valida dimensiones de bloque/cuadrícula (incluido z=1),
hilos por bloque del dispositivo y del kernel, usando cudaGetDeviceProperties y
cudaFuncGetAttributes. Con caché residente devuelve la geometría ya validada.
La consulta estática `CudaBackend::launchConfiguration(w,h,block,device)` valida
sin reservar tableros. No garantiza VRAM suficiente para construirlos.

`setCudaBlockSize` y `CudaBackend::setBlockSize` validan un candidato antes de
publicarlo; con caché residente cambian solo la geometría, conservando buffers,
contador GPU y stream. Ante rechazo o error de consulta se conservan selección,
caché y estado CPU confirmado. Edición/clear/cambio de motor invalidan la caché
como antes; su reconstrucción usa el bloque seleccionado. Las copias conservan
selección y buffers CPU independientes sin consultar GPU; los movimientos
transfieren selección y propietario GPU exclusivo.

Consultar geometría efectiva o cambiar el bloque requiere CUDA compilado y un
dispositivo visible, incluso si Game está en CPU. OFF y ausencia de dispositivo
producen error explícito; una forma no soportada se rechaza antes de consultar
CUDA. Para planificación sin toolkit/GPU existe `planCudaLaunch(w,h,block)` en la
biblioteca CPU `conway_cuda_launch`: calcula geometría y valida aritmética, pero
no certifica los límites de un dispositivo. El header no incluye CUDA ni expone
referencias mutables a buffers.

## Contrato y propiedad

`core::CudaBackend(InputData, generation = 0, device = 0, block = kDefaultCudaBlock)` valida las mismas
dimensiones, cantidad de células y valores binarios que `Game`, antes de acceder
al runtime o reservar los tableros. Posee mediante RAII dos buffers GPU con halo,
un contador de población y un stream propio. Es movible, no copiable; las llamadas
a una misma instancia deben serializarse.

Los buffers permanecen residentes entre pasos. `step()` no reserva memoria ni
transfiere tableros al host: limpia el contador, lanza el recorrido CUDA y reduce
la población. El mapeo es 2D: x recorre columnas, y recorre filas y cada hilo
representa una posición. Los hilos fuera del tablero aportan cero a la reducción
sin abandonar las barreras del bloque. Los halos de ambos buffers se inicializan
a cero y nunca se escriben. La sincronización y los errores preceden al único
swap y al incremento de generación. Ya no se usa el recorrido lineal grid-stride
ni el límite histórico de 1024 bloques. Se rechaza una cuadrícula que exceda los
límites reales; no se trunca ni se omiten células.

`snapshot()` copia por valor ambos buffers a un `GameState` CPU independiente,
con stride, generación y población. No contiene handles GPU. Editarlo, continuar
con el motor CPU, copiarlo o conservarlo después de mover/destruir el backend no
modifica el propietario GPU. Una nueva entrada requiere construir otro backend;
Game reconstruye su caché después de edición/clear/cambio de motor.

La evaluación pura B3/S23 se comparte en `core/detail/bs23_rule.hpp`, sin cabeceras
del runtime. Nvcc puede generar su versión host/device; los compiladores C++
ordinarios ven una función inline normal. Las dos APIs existentes de
`next_cell_state`, incluida captura de vecinos, conservan sus firmas y delegan
a esa única evaluación. No se modificaron los recorridos Secuencial/OpenMP/MPI.

Los fallos CUDA producen `CudaError`, con código y operación. Una instancia con
fallo de ejecución/transferencia se invalida: no promete rollback de un kernel
parcial y debe reconstruirse. Un error no publica el swap/generación. La generación
usa el wrap definido de `size_t`, igual que `Game`; no añade una excepción al llegar
al máximo. Los desbordamientos de dimensiones/halos se rechazan antes de reservar.
La liberación RAII es de mejor esfuerzo, no lanza excepciones ni resetea el
dispositivo. Las transferencias pendientes se sincronizan durante el unwind para
no liberar antes su almacenamiento host. Las operaciones seleccionan el device
del propietario y restauran la selección del hilo llamante.

## Compilar sin CUDA

Desde la raíz del repositorio, con GCC/G++ 12 y Open MPI de Ubuntu:

```bash
OMPI_CC=/usr/bin/gcc-12 OMPI_CXX=/usr/bin/g++-12 cmake -S . -B ../build-conway-cpu \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DMPI_CXX_COMPILER=/usr/bin/mpicxx -DBUILD_TESTING=ON \
  -DCONWAY_ENABLE_CUDA=OFF
cmake --build ../build-conway-cpu -j2
env -u NO_COLOR ctest --test-dir ../build-conway-cpu --output-on-failure
```

OFF es el valor predeterminado. No detecta el lenguaje CUDA, no busca CUDAToolkit
ni enlaza cudart. Se verificó además con la búsqueda de CUDAToolkit deshabilitada
y un compilador CUDA inexistente, sin cambiar el compilador global.

## Compilar con toolkit (GPU no requerida durante la configuración)

```bash
OMPI_CC=/usr/bin/gcc-12 OMPI_CXX=/usr/bin/g++-12 cmake -S . -B ../build-conway-cuda \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DMPI_CXX_COMPILER=/usr/bin/mpicxx -DBUILD_TESTING=ON \
  -DCONWAY_ENABLE_CUDA=ON \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.5/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12 \
  -DCONWAY_CUDA_ARCHITECTURES=86
cmake --build ../build-conway-cuda -j2
ctest --test-dir ../build-conway-cuda -R '^(cuda_|game_cuda_)' --output-on-failure
```

El host compiler debe fijarse en la primera configuración de un build nuevo.
Se exige nvcc >= 12.5; esta etapa se validó con Toolkit 12.5.1 (nvcc 12.5.82),
G++ 12.4.0 y CMake 3.28.3. CUDA usa C++20 obligatorio y arquitectura 86 configurable
mediante `CONWAY_CUDA_ARCHITECTURES` (lista CMake). Son propiedades del target,
`75` corresponde a la futura GTX 1650; `86`, a esta RTX 3050. Sin flags CUDA globales ni `--allow-unsupported-compiler`. Cambiar compiladores
requiere otro directorio de build. OpenMP y MPI siguen siendo requisitos CPU.

ON sin compilador/toolkit es error de configuración. Tener toolkit no implica
tener una GPU: `cuda_device_status()` distingue Available, NoDevice,
DriverUnavailable y RuntimeError, conservando código y diagnóstico. No convierte
errores de driver/runtime en una omisión exitosa.

## Pruebas y límites

- `cuda_input_validation`: errores de dimensiones, producto, cantidad y valores,
  antes de tocar el runtime; contrato de propiedad movible/no copiable.
- `cuda_no_device`: con `CUDA_VISIBLE_DEVICES=-1`, exige NoDevice y excepción al
  construir un backend válido; las entradas inválidas siguen validándose primero.
- `cuda_equivalence`: con GPU, compara exactamente contra `Game` Secuencial y el
  motor CPU: ambos buffers completos, halos cero, células, población y generación.
  Cubre 11 dimensiones (incluidos 1×1, 1×257, 257×1, 17×19, 31×33 y 513×517),
  cinco distribuciones (vacío, lleno, damero, aleatorio reproducible y bordes),
  Block/Blinker/Glider/Toad/Beacon centrados y en bordes, 512 vecindarios de regla,
  generaciones sucesivas y continuación CPU. Comprueba snapshots independientes,
  movimiento/asignación entre propietarios, destrucción repetida, dispositivos
  inválidos y wrap de generación equivalente a Game.
- `cuda_launch_geometry`: planificación CPU pura de los tres presets, redondeo,
  recuento de posiciones exteriores y rechazo de dimensiones/configuraciones inválidas.
- `game_cuda_contract`: compara ambos buffers completos y halos con el oráculo
  CPU para cada preset, cambios de geometría y reconstrucción de cachés; conserva
  selección en copias/movimientos de Game y backend. Incluye 512 vecindarios por
  preset, dimensiones
  degeneradas/irregulares, wrap, copias/asignaciones independientes, movimientos,
  referencias Game& retenidas, edición/clear, alternancia CPU/CUDA y rechazo de trazas.
- `game_cuda_unavailable`: exige excepción sin publicación ni fallback y recuperación
  CPU; se ejecuta con GPU oculta, también en el build OFF.
- `cuda_failure_cleanup` añade rollback/reintento de Game ante fallos de reserva,
  lanzamiento, ejecución, cada transferencia de snapshot y sincronización; incluye
  `bad_alloc` CPU en cada reserva de los dos buffers después de avanzar GPU y
  fallo de asignación por copia. Comprueba ambos buffers/halos y contadores con
  un acceso const solo definido en tests. También valida límites de bloque y
  cuadrícula/devices/kernel sintetizados, límites reales de grid x/y sin reservar
  tableros gigantes y cambio de bloque sin reconstruir reservas GPU. Wrapping
  y sustitución de operator new están confinados al ejecutable de pruebas.
- `cuda_failure_cleanup` (Linux con linker GNU compatible): quince fallos de API
  controlados con `--wrap`, aislados del backend de producción. Verifica construcción
  parcial (cada reserva, stream, transferencias y sincronización), drenaje de
  transferencias host, código/operación del error, invalidación por fallos de
  lanzamiento/ejecución/snapshot y liberación de recursos, también tras movimientos.
  No provoca accesos ilegales, agotamiento de VRAM ni pérdida real del dispositivo.

La equivalencia devuelve 77/Skipped únicamente si no hay dispositivo visible.
Un fallo de driver, runtime o kernel hace fallar la prueba. Puede verificarse con:

```bash
CUDA_VISIBLE_DEVICES=-1 ctest --test-dir ../build-conway-cuda -R '^(cuda_|game_cuda_)' --output-on-failure
```

**Compute Sanitizer permanece pendiente**: en WSL el host Windows no tiene
habilitada la interfaz de depuración WDDM. No se cambió el registro y no se afirma
que la comprobación de memoria esté aprobada. Equivalencia funcional no sustituye
esa comprobación. No se forzó agotamiento de VRAM ni pérdida de dispositivo para
inyectar errores reales. Las pruebas de fallos sintetizan retornos CUDA mientras
las reservas, transferencias y kernels normales usan el runtime real. Esto valida
las rutas de error y los recursos observados, no todos los efectos de un contexto
fatal ni la ausencia de accesos inválidos. La selección/restauración multi-GPU
solo se ejecutó con una GPU.

## Base de pruebas anterior a CUDA

`engine_selection_smoke` seguía enviando `C → ruta` y esperando `Entrada cargada:`.
El catálogo vigente abre una vista previa; `U` confirma la entrada inicial y emite
`Patrón confirmado:`. Se actualizó solo la prueba, incluyendo la persistencia de
OpenMP al confirmar ambos patrones.

`ftxui_benchmark_pty` estaba justo al borde de su timeout de 150 s. El helper PTY
esperaba siempre 350 ms por lectura incluso después de terminar la salida. El
perfil del recorrido ajustado registró 396 llamadas: con ese número de lecturas,
la espera fija anterior por sí sola impondría al menos 138,6 s. El helper admite ahora un límite de silencio optativo; solo el benchmark
lo usa (50 ms tras el último dato, máximo 350 ms). Si no llega salida conserva
350 ms, incluyendo las comprobaciones de ausencia de repintado. Los plazos de
espera de fases asíncronas y el timeout CTest de 150 s se conservaron.

El recorrido completo pasó en 39,56 s (36,65 s de lectura PTY), y después CTest
aprobó selección, encabezado y benchmark en 53,92 s, con benchmark de 39,03 s.
No se modificó comportamiento de producción para resolver estas incidencias.

Los resultados resumidos y los comandos de reproducción se conservan en esta guía.
Los registros locales, builds y binarios no se versionan ni son necesarios para
continuar en otro equipo. Véase [el checkpoint portable](CUDA_CHECKPOINT.md).

## Validación histórica del backend independiente (2026-10-28)

Base `develop`: `9a847178caadadcfa69e298a40f120a09d690362`; rama local
`feature/cuda`. Estos resultados se obtuvieron antes de publicar el checkpoint.
Ubuntu-24.04/WSL2, RTX 3050 (8.6), GCC/G++ 12.4.0, CMake 3.28.3 y Open MPI 4.1.6.

| Configuración | Resultado |
|---|---|
| CUDA OFF, todos los targets | Compilación aprobada; sin buscar toolkit y con nvcc inexistente |
| Suite CPU/OpenMP/MPI completa | 60/60 aprobadas, 807,75 s |
| Benchmark PTY final | Aprobado, 38,41 s, timeout original 150 s |
| CUDA ON, todos los targets | Compilación aprobada; nvcc 12.5.82, host G++ 12, C++20, arquitectura 86 |
| CUDA ON: CUDA + coexistencia CPU/MPI | 8/8 aprobadas, incluidas las tres pruebas CUDA |
| GPU oculta explícitamente | Validación y ausencia aprobadas; equivalencia Skipped |
| Compilador CUDA marcado NOTFOUND, ON | Error de configuración esperado con diagnóstico explícito |
| Compute Sanitizer | Pendiente; no aprobado |

Los JUnit y comandos reales quedaron como registros locales no versionados; los
resúmenes anteriores y las instrucciones permiten reproducirlos sin esos archivos.
En esa etapa, la aplicación existente no adquiría dependencia `cudart` incluso en el build ON;
solo el backend y sus tests la enlazan. No se hicieron mediciones de rendimiento
del motor CUDA.

## Revisión posterior, antes de integrar Game

Se corrigió la discrepancia del contador al llegar a `size_t::max`: CUDA lanzaba
una excepción y Game hacía wrap a cero. Ambos se comparan ahora también en ese
límite. La suma del helper B3/S23 vuelve a usar `int` como el original; evita que
bytes no binarios suministrados directamente al helper produzcan nacimientos
por wrap, conservando también su captura histórica. No afecta a las entradas
binarias validadas por Game.

La prueba de vida útil toma el oráculo CPU antes de destruir el propietario GPU,
compara ambos buffers y contadores después y vuelve a comprobarlo tras reutilizar
recursos. Incluye movimiento, asignación y automovimiento. La inyección de fallos
no añade interruptores ni flags a producción.

En esta revisión se ejecutaron diez regresiones CPU/MPI y cinco comprobaciones
de CUDA/regla, sin repetir la suite completa: todas aprobadas. Se conservaron
las aserciones de selección de motor y del benchmark PTY, con timeout de 150 s;
el benchmark pasó en 39,02 s. Con dispositivos ocultos, dos pruebas CUDA se
aprobaron y dos quedaron Skipped explícitamente. Las órdenes de reproducción están
en [CUDA_CHECKPOINT.md](CUDA_CHECKPOINT.md); no requieren los registros locales.
La suite 60/60 de la tabla anterior corresponde a la validación previa.
Compute Sanitizer continúa pendiente; esta revisión no certifica memoria.

## Validación de la integración (2026-10-29)

Todos los targets compilaron ON/OFF; RTX 3050: 7/7 CUDA/Game/regla,
14/14 regresiones OFF y 17/17 ON aprobadas. Con GPU oculta: tres pruebas
aprobadas y tres Skipped. También compiló `game_cuda_tests` con arquitectura 75,
sin ejecución en GTX 1650. Compute Sanitizer y el clúster CPU entre laptops
siguen pendientes. Alcance, comandos y resultados detallados en
[CUDA_CHECKPOINT.md](CUDA_CHECKPOINT.md#validación-de-la-integración-game-2026-10-29).

## Validación de bloques configurables (2026-10-29)

RTX 3050/86: suite completa ON 67/67 y OFF 63/63, sin fallos, omisiones ni tests
deshabilitados. Las ocho selectivas CUDA/Game/regla pasaron; con GPU oculta,
cuatro pasaron y tres se omitieron explícitamente. Se compiló y enlazó para 75,
sin ejecución en GTX 1650. Los tests permanentes cubren ahora ambos buffers
completos de Game, halos y errores bad_alloc en cada buffer de snapshot.
Compute Sanitizer y validación en GTX 1650 siguen pendientes. No se midió
rendimiento ni ocupación. Detalles y comandos de esta versión en
[CUDA_CHECKPOINT.md](CUDA_CHECKPOINT.md#etapa-de-configuración-programática-de-bloques-2026-10-29).

## CUDA en selección y simulación normal (2026-10-29)

CUDA se selecciona con M → G (o con las flechas y Enter). El menú muestra un
resultado guardado de disponibilidad: build sin CUDA, GPU ausente/oculta, error
real de inicialización/configuración o nombre de GPU disponible. La comprobación
se hace al abrir/confirmar selección y configuración, no desde Render. Usa un
backend temporal 1×1 para comprobar inicialización sin avanzar el patrón.

V abre la simulación normal del patrón confirmado. Reutiliza SimulationSession
y el renderizador de tableros existentes; la previsualización del catálogo sigue
siendo Secuencial y U confirma siempre la entrada inicial, nunca su fotograma.
La sesión normal se conserva al volver al menú: V vuelve a la misma generación,
motor y bloque, en pausa. R reinicia deliberadamente y confirmar otro patrón
crea una sesión nueva. La persistencia dura la ejecución de la aplicación; no se
añadió persistencia en disco.

Controles FTXUI: N paso en pausa; Esp/P pausa/reproduce; R reinicia; flechas mueven
el viewport; M cambia motor; B configura los recursos del motor; U recupera una
sesión con error; Esc/X vuelve al menú sin reiniciar. En el menú principal R abre
recursos. CUDA tiene presets 1=8×8, 2=16×16 (predeterminado), 3=32×8, y Enter
sobre el menú confirma; Esc cancela. OpenMP conserva sus hilos y MPI sus procesos.
Abrir ajustes pausa; confirmar un motor o bloque conserva tablero y generación.
Salir de ajustes no reanuda automáticamente.

La vista muestra el snapshot CPU confirmado por un paso real CUDA, sin NEXT
especulativo CUDA ni trazas didácticas. Muestra motor, GPU, bloque, hilos por bloque
y cuadrícula, en dos líneas de geometría en 40×18. No son hilos simultáneos ni
ocupación. Los datos se guardan en acciones explícitas y se reutilizan al renderizar.

Los errores de paso pausan la sesión y conservan ambos buffers/contadores CPU.
Se muestra el diagnóstico, sin seleccionar otro motor automáticamente. Recuperar
reconstruye desde el estado confirmado. Se corrigieron dos ventanas de transición
asíncrona: no publicar un Game incompleto si falla su configuración y no cambiar
motor/bloque si no se puede reservar/enviar el mensaje; recuperar una transición
fallida debe fijar CUDA explícitamente. MPI sigue siendo un motor CPU externo;
no hay cálculo MPI+CUDA.

La etapa posterior añade D didáctico CUDA manual sobre la misma sesión de V;
se describe al final de esta guía. Los benchmarks existentes mantienen sus
participantes CPU; no se añadió benchmark CUDA.

### Abrir manualmente esta copia en WSL

Desde una terminal interactiva Ubuntu/WSL, con los builds ya configurados:

```bash
cd /home/jean/projects/conway-parallel
cmake --build /home/jean/cuda-setup/build/game-on -j2
env -u CUDA_VISIBLE_DEVICES -u NO_COLOR TERM=xterm-256color \
  /home/jean/cuda-setup/build/game-on/conway
```

Secuencia comprobada: C → M → escribir `patterns/blinker.txt` → Enter → U;
M → G para CUDA; R → 2 para 16×16; V abre la simulación; N avanza a generación 1.
B → 1 cambia a 8×8 sin avanzar. Esc vuelve al menú y V conserva generación 1 y
bloque 8×8. M → O cambia a OpenMP sin reiniciar; M → G vuelve a CUDA con 8×8.
P reproduce/pausa, R reinicia a 0 y Esc → Q sale. En B, Esc cancela el cambio.

Para probar el rechazo con GPU oculta (seleccionar M → G):

```bash
cd /home/jean/projects/conway-parallel
CUDA_VISIBLE_DEVICES=-1 /home/jean/cuda-setup/build/game-on/conway
```

Para el diagnóstico del build OFF:

```bash
cd /home/jean/projects/conway-parallel
/home/jean/cuda-setup/build/game-off/conway
```

En ambos casos se conserva el motor previo; la selección fallida no inicia CPU
como sustituto de CUDA. Puede elegirse Secuencial explícitamente y usar V.

El flujo textual se activa con stdin no interactivo (comandos terminados en Enter,
X vuelve al menú). Ejemplo CUDA completo reproducible:

```bash
cd /home/jean/projects/conway-parallel
printf 'C\nM\npatterns/blinker.txt\nU\nM\nG\nR\n1\nV\nN\nB\n3\nX\nV\nN\nX\nQ\n' | \
  env -u CUDA_VISIBLE_DEVICES /home/jean/cuda-setup/build/game-on/conway
```

La arquitectura sigue siendo configurable con CONWAY_CUDA_ARCHITECTURES=86 o 75
en un build separado. Para un equipo nuevo usar la configuración completa de
esta guía; los comandos anteriores reutilizan el build validado de esta PC.
GTX 1650 y Compute Sanitizer siguen pendientes. Cada generación CUDA es síncrona
y descarga un snapshot CPU completo: no se midió rendimiento ni ocupación. No se
cambiaron herramientas, drivers ni Windows.

## Validación final de la UI CUDA (2026-10-29)

Suite completa OFF: 66/66, 818,38 s. Suite completa ON en RTX 3050/86: 71/71,
838,33 s. Ambas sin fallos, omisiones ni tests deshabilitados. Incluyen selección,
persistencia, cancelación, pausa/reproducción, bloque, diagnóstico de ausencia,
40×18 y regresiones de controles existentes. Los errores de inicialización,
lanzamiento y reserva de mensaje se verifican por inyección, conservando ambos
buffers y recuperación sin fallback. Se corrigieron la etiqueta Q salir, el
recorte numérico del grid compacto y ventanas transaccionales del trabajador.
La ejecución interrumpida anterior no se cuenta como suite aprobada.

OFF mantiene 42 ejecutables sin dependencias CUDA. GTX 1650 y Compute Sanitizer
siguen pendientes; el backend/game_cuda_tests compila para 75, pero la aplicación
no se ejecutó en GTX 1650. No se probó pérdida real de GPU ni agotamiento real de
VRAM. No hay mediciones de rendimiento ni CUDA didáctico/benchmark. Resultados,
alcance y comandos CTest en [CUDA_CHECKPOINT.md](CUDA_CHECKPOINT.md).

## Didáctico CUDA manual (2026-10-29)

Con CUDA seleccionado (M → G), D abre el didáctico; también D desde la simulación
normal. Se reutiliza su misma SimulationSession/Game: no se duplica el propietario
GPU ni se captura una generación anticipada. La reproducción normal se pausa y
se descartan sus ticks pendientes al entrar. No hay reproducción automática de
la selección ni controles de velocidad en esta versión.

El aviso «Exploración conceptual; no representa el orden de ejecución GPU» queda
visible en tablero y detalle, también al reducir la terminal. El tablero usa las
primitivas existentes de célula/borde, con divisiones entre bloques, contorno del
bloque seleccionado, borde doble de la célula y borde grueso de vecinos. Las
posiciones exteriores usan ×. Las divisiones no son fronteras muertas: vecinos
válidos de otros bloques participan normalmente en B3/S23.

| Control en el didáctico CUDA | Acción |
|---|---|
| [ / ] | Bloque anterior/siguiente, conservando el hilo; sin wrap en extremos |
| H/J/K/L | Hilo izquierda/abajo/arriba/derecha dentro del bloque |
| N | Siguiente hilo del mismo bloque; se detiene al final |
| G | Una generación CUDA real, no un paso de exploración |
| B | Preset 1=8×8, 2=16×16 predeterminado, 3=32×8; Enter/atajo confirma, Esc cancela |
| Flechas | Viewport; la siguiente selección vuelve a seguir el cursor |
| V | Alternar tablero y detalle ampliado |
| PgUp/PgDn, Home | Desplazar detalle; Home vuelve al inicio |
| R | Entrada inicial, generación 0, selección (0,0)/(0,0), conservando preset |
| U | Recuperar CUDA sin avanzar ni cambiar a CPU |
| Esc/X/Q | Menú, conservando sesión/configuración/selección; vuelve pausado |

Todos los índices y coordenadas CUDA son base cero. El panel muestra blockIdx,
threadIdx y las fórmulas columna=blockIdx.x*blockDim.x+threadIdx.x y
fila=blockIdx.y*blockDim.y+threadIdx.y. Para una célula válida muestra estado,
vecinos y predicción identificada como CPU con la regla compartida B3/S23.
El adaptador reúne como máximo nueve valores válidos en un stencil 3×3 y llama
core::next_cell_state con CellEvaluation, el mismo evaluador compartido con el
kernel; no duplica la regla ni expone buffers mutables.

Una posición exterior se comprueba antes de invocar cualquier lector: no se
consulta ninguna célula, ni se evalúa una regla. El kernel inspeccionado mantiene
alive=0 en esa rama, y después todos los hilos escriben su parcial y alcanzan las
barreras de reducción; no retorna anticipadamente. El panel lo explica así, sin
atribuir orden de ejecución, simultaneidad ni ocupación GPU.

Explorar no cambia current, next, generación o población. El frame es pequeño,
sin vectores de tamaño tablero, y el detalle se calcula en eventos, no al renderizar.
El renderer recibe Game const& y solo dibuja el rectángulo visible de la cobertura
virtual. No copia Game, reconstruye mapas completos ni consulta CUDA. La
instrumentación de tests verifica que exploración/render no llaman consultas de
conteo/dispositivo/propiedades/atributos ni reservas GPU. G reutiliza step CUDA sin
sink: publica ambos buffers/contadores solo tras éxito completo y actualiza después
el detalle. El siguiente detalle es una nueva predicción CPU, no una traza GPU.

Cambiar preset conserva coordenada global si cabe en la nueva cobertura. Si deja
de caber, la ajusta a una célula válida y lo indica como Ajuste. R es transaccional;
volver al menú, a V normal y a D conserva la misma generación. Un error CUDA
mantiene el último frame confirmado y permite explorar ese estado, mostrar el
diagnóstico con V y recuperar con U; no se reintenta el paso automáticamente.
Cambiar patrón descarta la selección anterior. Redimensionar cambia solo layout y
viewport. En 40×18 se priorizan tablero y detalle esencial; V muestra la explicación
completa y desplazable. Bajo el mínimo se muestra el aviso y se conserva el estado.

### Probar el ejemplo 35 columnas × 19 filas

```bash
cd /home/jean/projects/conway-parallel
python3 - <<'PYEXAMPLE'
from pathlib import Path
rows = [['0'] * 35 for _ in range(19)]
for row, col in [(5,15),(5,16),(5,17),(17,33),(17,34),(18,34)]:
    rows[row][col] = '1'
Path('/tmp/conway-cuda-35x19.txt').write_text(
    '19 35\n' + '\n'.join(''.join(row) for row in rows) + '\n')
PYEXAMPLE
cmake --build /home/jean/cuda-setup/build/game-on --target conway -j2
env -u CUDA_VISIBLE_DEVICES -u NO_COLOR TERM=xterm-256color \
  /home/jean/cuda-setup/build/game-on/conway
```

C → M → `/tmp/conway-cuda-35x19.txt` → Enter → U; M → G; R → 2; D.
Con 16×16: grid 3×2, cobertura 48×32, 1536 posiciones y 871 exteriores.
Pulsa ] cinco veces para blockIdx=(2,1), L dos veces y J dos veces para
threadIdx=(2,2): columna 34, fila 18, célula válida con 2 vecinos y predicción CPU
1→1. Una L más selecciona columna 35: exterior, sin evaluación. V muestra
fórmulas, vecinos/regla o explicación de reducción/barreras. G avanza toda la
generación real aun si la posición seleccionada es exterior.

B → 1 muestra 8×8/grid 5×3/295 exteriores; B → 3 muestra 32×8/grid 2×3/871
exteriores. No avanza la generación. Esc → D conserva el estado; Esc → V abre
la misma generación en vista normal. P reproduce allí; D vuelve a pausarla.
R en el didáctico reinicia; Esc → Q sale. El modo didáctico sigue requiriendo una
terminal interactiva FTXUI; el flujo textual previo permanece sin modo didáctico.

La primera versión mantiene el paso CUDA síncrono existente; en un tablero grande
puede demorar controles durante G. GTX 1650 y Compute Sanitizer siguen pendientes.
No hay benchmark nuevo, medición de ocupación ni cambios de herramientas/drivers.

### Validación del didáctico manual

ON (RTX 3050/86): 20/20 específicas y regresiones afectadas, 268,41 s; OFF: 9/9,
48,62 s. Cero fallos, omisiones o deshabilitadas en esas selecciones. La integración
GPU en OFF se comprobó aparte y quedó Skipped explícitamente; no se considera
aprobada. Incluye 35×19 en los tres presets, 512 vecindarios, buffers completos,
exteriores sin lecturas, render/exploración sin consultas CUDA ni reservas GPU,
G real, errores/bad_alloc/recuperación, controles heredados, persistencia y resize.
La captura de la prueba PTY se corrigió para descartar filas del raster previo al
redimensionar; la versión final pasó sin eliminar aserciones. OFF sigue aislado
(44 ejecutables sin libcuda/cudart). No se repitieron las suites completas previas.
Resultados, comandos y límites detallados en CUDA_CHECKPOINT.md.

### Presentación común y cobertura opcional

El didáctico CUDA reutiliza el renderer de rejilla y geometría adaptable de los
paneles didácticos existentes, con decoraciones opcionales. Sin decoraciones se
conserva la salida original (pruebas de raster contra capturas previas, con y sin
color). @1/@0 distingue selección viva/muerta; :1/:0 distingue vecinos. Ambos
caben en células de 2 a 8 caracteres sin cambiar sus dimensiones. Contorno cian
para bloque seleccionado, invertido sin color; los interiores conservan el estilo
original salvo los nueve marcadores posibles. Límites reales gruesos, borde de
tablero parcial fino y recorte de viewport discontinuo; un recorte coincidente
con límite real se representa como recorte y se explica en el detalle.

A alterna un esquema secundario acotado, no una matriz de células exteriores.
En compacto solo añade A:+cantidad a la leyenda; V permite consultar los números
y el esquema. Activar A conserva exactamente el rectángulo y los píxeles del
tablero real, incluso en 40×18. El hilo exterior no recibe una célula sustituta.
El aviso conceptual, la predicción CPU compartida y el avance CUDA real con G
se mantienen. No cambian kernel, buffers, generación ni evaluación B3/S23.

Para abrir la versión actualizada:

```bash
cd /home/jean/projects/conway-parallel
cmake --build /home/jean/cuda-setup/build/game-on --target conway -j2
env -u CUDA_VISIBLE_DEVICES -u NO_COLOR TERM=xterm-256color COLORTERM=truecolor \
  /home/jean/cuda-setup/build/game-on/conway
```

Cargar patrón con C → M → ruta → Enter → U; M → G selecciona CUDA; D abre
el didáctico. HJKL selecciona hilo, [/] bloque, B cambia preset, A cobertura,
V detalle, G avanza, R reinicia, Esc vuelve. Para revisar sin color, añadir
`NO_COLOR=1` al entorno de ejecución (sin `-u NO_COLOR`).

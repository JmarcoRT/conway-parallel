# CUDA: primera etapa, backend residente opcional

El backend es independiente de la aplicación: no añade `Engine::CUDA`, selección
FTXUI, trazas didácticas ni campañas de rendimiento. `Game` y sus copias conservan
su comportamiento. El nuevo target `conway_cuda` solo existe al solicitarlo.

## Contrato y propiedad

`core::CudaBackend(InputData, generation = 0, device = 0)` valida las mismas
dimensiones, cantidad de células y valores binarios que `Game`, antes de acceder
al runtime o reservar los tableros. Posee mediante RAII dos buffers GPU con halo,
un contador de población y un stream propio. Es movible, no copiable; las llamadas
a una misma instancia deben serializarse.

Los buffers permanecen residentes entre pasos. `step()` no reserva memoria ni
transfiere tableros al host: limpia el contador, lanza el recorrido CUDA y reduce
la población. El grid-stride traversal procesa solo células lógicas; los halos de
ambos buffers se inicializan a cero y nunca se escriben. La barrera del stream y
la comprobación de errores preceden al único swap y al incremento de generación.
El límite de bloques es 1024 (o el límite menor del dispositivo), con 256 hilos
por bloque. No se exige que las dimensiones sean múltiplos del bloque.

`snapshot()` copia por valor ambos buffers a un `GameState` CPU independiente,
con stride, generación y población. No contiene handles GPU. Editarlo, continuar
con el motor CPU, copiarlo o conservarlo después de mover/destruir el backend no
modifica el propietario GPU. Una nueva entrada requiere construir otro backend;
esta etapa no adapta edición/clear/sesiones de la aplicación al dispositivo.

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
ctest --test-dir ../build-conway-cuda -R '^cuda_' --output-on-failure
```

El host compiler debe fijarse en la primera configuración de un build nuevo.
Se exige nvcc >= 12.5; esta etapa se validó con Toolkit 12.5.1 (nvcc 12.5.82),
G++ 12.4.0 y CMake 3.28.3. CUDA usa C++20 obligatorio y arquitectura 86 configurable
mediante `CONWAY_CUDA_ARCHITECTURES` (lista CMake). Son propiedades del target,
sin flags CUDA globales ni `--allow-unsupported-compiler`. Cambiar compiladores
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
- `cuda_failure_cleanup` (Linux con linker GNU compatible): quince fallos de API
  controlados con `--wrap`, aislados del backend de producción. Verifica construcción
  parcial (cada reserva, stream, transferencias y sincronización), drenaje de
  transferencias host, código/operación del error, invalidación por fallos de
  lanzamiento/ejecución/snapshot y liberación de recursos, también tras movimientos.
  No provoca accesos ilegales, agotamiento de VRAM ni pérdida real del dispositivo.

La equivalencia devuelve 77/Skipped únicamente si no hay dispositivo visible.
Un fallo de driver, runtime o kernel hace fallar la prueba. Puede verificarse con:

```bash
CUDA_VISIBLE_DEVICES=-1 ctest --test-dir ../build-conway-cuda -R '^cuda_' --output-on-failure
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

## Validación de esta etapa (2026-10-28)

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
La aplicación existente no adquiere dependencia `cudart` incluso en el build ON;
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

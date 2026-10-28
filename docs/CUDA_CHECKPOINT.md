# Checkpoint de feature/cuda

## Estado terminado

Base: `develop` en `9a847178caadadcfa69e298a40f120a09d690362`.
Esta rama contiene un backend CUDA independiente y revisado. **La integración en
Game no se ha implementado.** No hay selección CUDA en FTXUI ni benchmarks de
rendimiento. Secuencial, OpenMP y MPI conservan sus rutas existentes.

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
  -R '^(cuda_.*|rule_bs23_per_cell)$'
CUDA_VISIBLE_DEVICES=-1 ctest --test-dir ../build-conway-cuda \
  -R '^cuda_' --output-on-failure
```

Con GPU visible, las cinco comprobaciones seleccionadas deben pasar en el entorno
Linux validado. Con GPU oculta: dos pruebas CUDA pasan y dos se omiten explícitamente.
Los errores de driver/runtime hacen fallar las pruebas; no se convierten en Skipped.
`cuda_failure_cleanup` requiere Linux y linker GNU compatible.

## Base verificable y pendientes

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

## Siguiente etapa mínima propuesta, todavía no implementada

1. Añadir CUDA solo como selección programática de Game y una caché GPU privada,
   opaca y exclusiva. GameState sigue siendo el estado CPU confirmado, sin handles.
   Asegurar configuración uniforme en los targets que compilan Game.
2. Copiar estado CPU y motor, dejando la caché GPU vacía para reconstrucción
   perezosa; copiar no debe consultar ni reservar GPU. Definir explícitamente copia,
   asignación y movimientos sin compartir propietarios. Invalidar caché al editar,
   hacer clear, avanzar por CPU o cambiar motor; conservar setEngine noexcept.
3. En step CUDA, ejecutar y obtener un snapshot candidato antes de publicar estado
   CPU. Ante cualquier fallo, incluido bad_alloc al obtener snapshot, descartar caché
   y conservar el último estado CPU para no reutilizar una GPU adelantada. Sin
   backend compilado o GPU, error explícito, sin fallback silencioso. Rechazar sink
   de trazas no nulo antes de mutar hasta definir su contrato CUDA.
4. Probar copias y asignaciones independientes, movimientos, edición/clear,
   alternancia Secuencial/OpenMP/CUDA, errores sin publicación y builds ON/OFF.
   MPI conserva su trabajador externo. Mantener FTXUI y benchmarks fuera de esta etapa.

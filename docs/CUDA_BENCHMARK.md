# Benchmark CUDA

CUDA participa en Comparación de motores junto con Secuencial, OpenMP y MPI.
Su configuración experimental es independiente del motor de la simulación.
Seleccione un bloque 8x8, 16x16 o 32x8. En Escalabilidad, CUDA compara una lista
sin duplicados de esos bloques en una GPU; no aumenta recursos físicos.
Resultados de la sesión conserva el último experimento con sus muestras y método.

## Medición

La ruta experimental usa `CudaBackend::runBatch`, que mantiene ambos buffers
en GPU durante el lote, reutiliza el kernel B3/S23 y realiza una sincronización
final. No copia tableros ni reserva buffers por generación. `Game::step()` y
las vistas normal y didáctica conservan sus snapshots por paso.

El tiempo principal es `evolution_wall_ms` (campo común `elapsed_ms`): reloj CPU
desde el registro del evento inicial hasta completar el trabajo del stream.
Incluye el encolado, los eventos, limpiezas del contador y sincronización final;
excluye selección de dispositivo, creación de eventos, consultas de hardware,
preparación, snapshot, comparación, estadísticas, CSV y render.

Los eventos se registran en el stream propio alrededor del lote.
`cuda_device_ms` es ese intervalo, incluidas posibles pausas de encolado; no es
ocupación, simultaneidad ni una suma certificada del tiempo exclusivo de kernels.

Tiempos auxiliares por muestra:

- `cuda_prepare_ms`: constructor del backend, reservas y carga inicial.
- `cuda_snapshot_ms`: creación y descarga del estado final completo.
- `cuda_total_operation_ms`: desde antes de preparar hasta después de liberar
  explícitamente los recursos; incluye costes entre intervalos y creación y
  destrucción de eventos. Excluye consultas iniciales de hardware y equivalencia.

El total se mide directamente. Los auxiliares no son una descomposición aditiva
exacta y no se suman al principal para calcular speedup. La preparación del
contexto puede ocurrir durante la comprobación de disponibilidad, fuera de estos
relojes; no se presentan estos datos como tiempos de arranque frío del driver.

Cada calentamiento y muestra comienza en generación cero desde la misma entrada,
con un backend nuevo preparado fuera del principal. El orden rota por ronda.
Los calentamientos se verifican, pero no forman parte de las estadísticas.
Dimensiones, generación, población y todas las celdas finales se contrastan con
la referencia secuencial fuera del reloj. Los presupuestos de payload CPU/GPU
se validan por separado; no representan RSS ni una garantía de VRAM libre.

Speedup = mediana secuencial / mediana principal del participante, solo con
referencia válida y medianas positivas. La eficiencia CUDA es N/D: los hilos por
bloque y las posiciones lanzadas no son recursos físicos equivalentes a procesos
MPI o hilos OpenMP. No hay un requisito de aceleración mínima ni un bloque ganador
universal; tableros pequeños pueden resultar más lentos en GPU.

## Disponibilidad y errores

Una compilación sin CUDA o una GPU ausente se informa explícitamente. La UI
muestra el diagnóstico al elegir CUDA; la API valida disponibilidad antes de
calcular la referencia o ejecutar muestras. No hay fallback CPU bajo etiqueta CUDA.

Los fallos de ejecución conservan el estudio parcial y detienen participantes
posteriores. La liberación de buffers, stream y eventos se comprueba; si no puede
confirmarse, el resultado lo declara y la UI bloquea nuevas mediciones. El driver
no se reinicia desde la aplicación.

Antes de medir, la UI pausa la simulación normal y libera su caché CUDA conservando
el estado CPU, motor y bloque. Volver a la simulación reconstruye esa caché al
avanzar. Una sesión MPI normal se cierra antes de iniciar otro experimento.

## Exportación y procedencia

Los estudios con CUDA usan CSV v3; los estudios CPU/MPI existentes conservan v2
y los resultados locales históricos v1. Además de los campos comunes, v3 guarda
bloque, grid, tiempos auxiliares y confirmación de limpieza CUDA. Campos de hilos
OpenMP y procesos MPI están vacíos para CUDA. La eficiencia lleva estado
`not_applicable_cuda`, no cero.

Metadatos: GPU, índice, capacidad de cómputo, memoria total, versiones runtime,
driver API y nvcc, variables CUDA_VISIBLE_DEVICES/CUDA_LAUNCH_BLOCKING y comando efectivo de compilación CUDA. La versión driver API
no es la versión comercial del driver Windows. Se registran alcances de tiempo,
protocolo v3 y presupuesto GPU. Exportar no vuelve a medir ni recalcula resultados.
La exportación sigue siendo por API; no se añade un botón interactivo.

## Compilación reproducible

Para Ubuntu 24.04 / WSL2, CUDA 12.5 y G++ 12, con arquitectura 86 (RTX 3050):

```bash
cmake --preset experimental-cuda-release
cmake --build --preset experimental-cuda-release
ctest --preset experimental-cuda-release --output-on-failure
./build-cuda-release/conway
```

Ajuste toolkit, compilador anfitrión y arquitectura si el entorno es distinto.
Todos los motores de una campaña se compilan con el mismo perfil C++ Release.
El preset histórico G++ 14 permanece disponible para campañas CPU; no mezclar
sus objetos o resultados con una campaña del perfil CUDA.

## Validación funcional

Las pruebas nuevas cubren lotes frente a Secuencial con buffers exactos, tamaños
irregulares y todos los bloques; ausencia de copias/reservas de tableros durante
el lote; errores de eventos y liberación; comparación de cuatro motores;
calentamientos, rotación y métricas; estudios de bloques; CSV v3 parseado; y
navegación real en formularios/resultados de terminal amplia y pequeña.
No son una campaña certificada de rendimiento ni una prueba de significancia.

El resumen separa participantes con espacios y títulos suaves; las explicaciones
largas de tiempos y métricas se consultan en Método. Muestras conserva el detalle
de cada ejecución y los costes auxiliares.

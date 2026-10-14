# Comparación experimental configurable — Fase 4

API independiente de FTXUI y MPI, en `app/experiment_runner.hpp`, target CMake
`conway_experiments`. Reutiliza los helpers privados de medición, equivalencia
y restauración OpenMP del benchmark, y las estadísticas de Fase 3.
`runBenchmark()` sigue teniendo su procedimiento y su interfaz históricos.

```cpp
#include "app/experiment_runner.hpp"

const auto generated = conway::input::generateInput({2000, 2000, 3000, 42});
conway::app::ExperimentConfig config;
config.generations = 100;
config.warmups = 2;       // Por motor; también admite cero.
config.repetitions = 5;   // Rondas medidas; dos ejecuciones por ronda.
config.requestedThreads = 4;
const auto result = conway::app::runExperiment(generated, config);
// También: runExperiment(existingInputData, config).
// result.summary.speedup y efficiency son optional<double>.
```

## Entrada y configuración

La entrada se genera **una sola vez por el llamante**, antes de ejecutar.
Los dos overloads reciben referencias constantes: no regeneran ni guardan
copias del tablero. Cada ejecución construye un Game nuevo desde la misma
entrada, en generación cero. La entrada debe permanecer inmutable durante
la llamada; no se permite modificación concurrente por otro propietario.

La configuración queda copiada en el resultado: generaciones positivas,
calentamientos no negativos, repeticiones e hilos positivos y política de orden
reconocida. Se verifican los tamaños del registro antes de ejecutar.

El ejecutor valida dimensiones, cantidad exacta de células y valores binarios.
Comprueba aritmética de dimensiones/halos, capacidad del vector y el límite
firmado del bucle OpenMP. No cambia las validaciones del cargador ni del
benchmark antiguo. Las comprobaciones del generador no se invocan generando
otro tablero: esta API debe proteger también entradas externas.

`ExperimentInputIdentity` conserva dimensiones y conteo vivo real. Su fingerprint
es FNV-1a-64: offset 14695981039346656037, primo 1099511628211, aritmética sin
signo módulo 2^64. Se procesan ancho y alto como enteros de 64 bits, cada uno
en ocho bytes little-endian, seguidos de las células binarias por filas.
Es una identificación estable para auditoría, **no una prueba criptográfica**
ni un sustituto de la equivalencia celda por celda.

Con `GeneratedInput` se conservan también semilla, densidades solicitada y
observada, y versión del generador en un string propio. Se comprueba la
coherencia de dimensiones, conteo, densidad observada y rango solicitado.
La procedencia declarada (semilla/versión) no se autentica regenerando: el
llamante debe usar los metadatos originales. El resultado sobrevive a la entrada.

## Orden y medición

1. Una ejecución secuencial calcula la referencia final, excluida de las muestras.
2. Se ejecutan todas las rondas de calentamiento, con ambos motores por ronda.
3. Se ejecutan las rondas medidas, con ambos motores por ronda.
4. Se calculan estadísticas y métricas después de terminar las mediciones.

`AlternatingSequentialFirst` produce S→O, O→S, S→O, O→S…;
`AlternatingOpenMPFirst` invierte esa secuencia. Los calentamientos usan la misma
política, pero el índice de ronda se reinicia al comenzar las mediciones.
Así la política medida no cambia al cambiar los calentamientos.

`ExperimentSample` contiene un `BenchmarkSample` original y tres identificadores:
`roundIndex` desde cero, `positionInRound` 0/1 y `executionIndex` global.
La referencia ocupa posición global 0; siguen `2*warmups` ejecuciones excluidas;
las muestras empiezan en `1 + 2*warmups`. El vector conserva el orden real de
adquisición, no se agrupa ni se ordena por duración. `repetitionIndex` coincide
con la ronda dentro de cada motor. `completedWarmupRuns` audita el número de
calentamientos terminados; sus tiempos no se guardan como muestras.

El helper `runOnce()` conserva ambos puntos de `steady_clock` y su cuerpo.
Cronometra el lote de llamadas a `Game::step()`, incluidos sincronización y
costes internos del motor. Como en el benchmark aprobado, incluye las consultas
existentes del equipo efectivo después de cada paso OpenMP y su acumulación
mínima/máxima. No añade consultas de reloj ni StepTrace. Construcción de Game,
identificación/generación, comparación final, inserción de muestras, renderizado
y estadísticas quedan fuera del intervalo. No hay renderizado en este módulo.

Se deshabilita el ajuste dinámico durante la ejecución y se aplica la solicitud
antes de cada ejecución OpenMP. Un guard RAII compartido restaura los ICV del
contexto llamante (`omp_get_max_threads()` y `omp_get_dynamic()`) al retornar o
propagar una excepción. No modifica variables de entorno ni pretende alterar
otros contextos OpenMP. Las restricciones del runtime siguen siendo observables.

## Observaciones, validez y métricas

Las observaciones conservan tiempo total en milisegundos como `double` original,
generaciones, solicitud OpenMP (ausente para secuencial), mínimo/máximo efectivos
y equivalencia individual con la referencia, comprobada fuera del reloj.

`validForComparison()` exige equivalencia, tiempo finito no negativo y metadatos
de trabajo/equipo válidos. `summarizeExperimentSamples()` es independiente de la
ejecución y permite comprobar las reglas con datos sintéticos. El resumen exige
además igual cantidad no vacía de muestras de ambos motores, mismas generaciones
y una solicitud OpenMP común. No elimina silenciosamente observaciones inválidas.

- Las estadísticas descriptivas reflejan todas las mediciones de cada motor,
  incluso si falló su equivalencia. Si contiene tiempos inválidos, las estadísticas
  de ese motor quedan ausentes. Se reutiliza la desviación **poblacional** de Fase 3.
- `equivalent` expresa el resultado agregado de la comparación de estados;
  `validForComparison` incorpora también la validez de tiempos y configuración.
- Speedup = mediana secuencial / mediana OpenMP, disponible solo si la comparación
  es válida, ambas medianas son positivas y el cociente es finito y positivo.
- Eficiencia = speedup / equipo efectivo, disponible solo si el equipo es positivo
  y constante dentro de cada muestra y entre todas las mediciones OpenMP.
- Cero es un tiempo observado válido; una mediana cero indica resolución
  insuficiente para las métricas. La ausencia se expresa con `nullopt`, no cero.
- Los equipos variables no invalidan por sí solos la comparación temporal, pero
  impiden informar una eficiencia representativa. Se conserva su rango observado.

Los resúmenes de equipos de este ejecutor **excluyen calentamientos**. El campo
histórico de `BenchmarkResult` sigue incluyéndolos y conserva su significado.

## Límites y validación

Alternar reduce el desequilibrio de posiciones; no elimina variaciones de carga,
frecuencia, temperatura, afinidad, caché o planificación. Un número impar de
rondas deja una posición adicional al motor inicial. La referencia secuencial
previa también afecta cachés. El orden es reproducible, los tiempos no lo son.

El payload máximo de tableros simultáneos es `N + 4*P`: entrada prestada y dos
Game (referencia y ejecución actual), con `P=(ancho+2)*(alto+2)`. Se comprueba
que esta suma no desborde. No se añade un límite arbitrario de RAM: el presupuesto
del generador cubre solo un Game; el llamante debe presupuestar ambos, pilas,
runtime y registro O(repeticiones). `bad_alloc` se propaga con restauración OpenMP.

CTest añade `experiment_runner` y `experiment_runner_thread_limit` (límite 1).
Cubren patrones existentes y generados, cambios de configuración, orden/rondas,
separación de calentamientos, estadísticas, equivalencia, equipos observados,
identificación, metadatos, restauración al terminar y ante fallo de asignación,
parámetros inválidos y compatibilidad con el benchmark antiguo. Las reglas de
validez se prueban con tiempos sintéticos, sin umbrales de rendimiento.

Este módulo no contiene UI ni exportación. El proyecto integra formularios y
vistas FTXUI, un módulo separado de escalabilidad y CSV por API.

La Fase 5 añade una API separada de escalabilidad fuerte con referencia
secuencial compartida: [SCALING_STUDY.md](SCALING_STUDY.md).

La captura de metadatos y exportación independiente de Fase 6 se documenta en
[EXPERIMENT_CSV.md](EXPERIMENT_CSV.md).

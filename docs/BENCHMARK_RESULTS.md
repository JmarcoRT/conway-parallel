# Muestras y estadísticas — Fase 3

Esta ampliación registra resultados; no cambia el protocolo experimental ni la
presentación. `runBenchmark()` y los campos históricos de `BenchmarkResult`
conservan su significado, sus fórmulas y sus consumidores (consola y FTXUI).

## Observaciones conservadas

`BenchmarkResult::samples` contiene primero la serie secuencial y después la
serie OpenMP, en orden de adquisición. Cada `BenchmarkSample` conserva:

- Motor e índice de repetición **desde cero dentro de cada motor**.
- `elapsedMs`: tiempo total del lote en milisegundos, como `double` original,
  sin redondeo; `generations`: generaciones ejecutadas en ese lote.
- `requestedThreads`: solicitud OpenMP; `nullopt` para el secuencial.
- Mínimo/máximo efectivos de esa ejecución, reutilizando las observaciones
  existentes. Para el secuencial ambos son 1.
- Equivalencia individual con la referencia secuencial, usando la comparación
  existente de dimensiones, generación, conteo y células fuera del cronómetro.
  Se obtiene para cada repetición aunque una ejecución previa haya fallado;
  el resultado agregado sigue siendo la conjunción de las equivalencias.

No se conservan como muestras ni los calentamientos ni la ejecución que crea
la referencia. El resumen histórico de hilos sigue incluyendo el calentamiento
OpenMP para mantener exactamente su semántica y el cálculo de eficiencia.
Por ello no debe reinterpretarse como un resumen exclusivo de `samples`.

## Estadísticas derivadas

`calculateBenchmarkStatistics(span<const double>)` es una función independiente
de la ejecución del benchmark. No modifica ni reordena las observaciones;
ordena una copia para obtener la mediana. Devuelve `optional<BenchmarkStatistics>`:

- Conjunto vacío: `nullopt`, sin centinelas cero.
- Tiempo negativo, NaN o infinito: `invalid_argument`.
- Cero: observación válida, incluida en todos los cálculos.
- Mínimo, máximo, mediana, media aritmética y desviación estándar **poblacional**
  (divisor N). Una sola observación tiene desviación cero.

Media y desviación se calculan con momentos incrementales normalizados por el
máximo, usando `long double` para evitar desbordamientos de acumulación. La
mediana par promedia los dos centrales; para valores extremos cuya suma desborde
se usa una expresión equivalente segura. No se redondea antes de los cálculos.

`sequentialStatistics` y `openMPStatistics` se calculan únicamente sobre las
repeticiones medidas. Sus medianas coinciden con los campos históricos en las
mediciones del benchmark. La precisión de los tiempos sigue siendo la de
`steady_clock` convertida a `double`, no una garantía de resolución física.

## Intervalo y limitaciones

El cuerpo de `runOnce()`, incluidos ambos puntos del reloj, permanece intacto.
Las reservas del registro, inserciones, comparaciones y estadísticas están
fuera del intervalo. Se conservan referencia, calentamientos, orden de motores,
repeticiones y consultas de equipo efectivo existentes.

El registro consume memoria proporcional a las repeticiones y añade trabajo
fuera del cronómetro; no elimina posibles efectos sobre cachés entre ejecuciones.
No se conectan entradas generadas ni se exportan datos. Permanecen las
limitaciones de validez de métricas y validación general de entradas documentadas
en `BENCHMARK_PROTOCOL.md`; esta fase no las corrige ni cambia su presentación.

Pruebas: datos sintéticos con cardinalidad par/impar, valores iguales, cero,
fracciones, extremos finitos y datos inválidos; compatibilidad de agregados
anteriores. Las pruebas runtime verifican orden, exclusión de calentamientos,
metadatos, medianas pares/impares, fórmulas y equipos observados tanto con varios
hilos como con `OMP_THREAD_LIMIT=1`, sin umbrales de rendimiento.

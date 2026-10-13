# Escalabilidad fuerte de OpenMP — Fase 5

API en `app/scaling_study.hpp`, target `conway_experiments`, independiente de
FTXUI/MPI. La entrada y las generaciones permanecen fijas para todo el estudio.
No se implementa como llamadas repetidas a `runExperiment()`.

```cpp
#include "app/scaling_study.hpp"

const auto board = conway::input::generateInput({2000, 2000, 3000, 42});
conway::app::ScalingConfig config;
config.generations = 100;
config.warmups = 2;
config.repetitions = 10;
config.requestedThreads = {1, 2, 4, 8};
config.order = conway::app::ScalingOrder::RotateLeft;
const auto study = conway::app::runScalingStudy(board, config);
// También acepta const InputData&. No regenera ni copia el tablero de entrada.
// Enlazar con conway_experiments y, si se genera, con conway_generated_input.
```

## Configuración, orden y referencia compartida

Generaciones y repeticiones deben ser positivas; calentamientos pueden ser cero.
La lista debe ser no vacía, sin duplicados, y cada solicitud debe estar entre
1 e INT_MAX. Se recibe como `vector<int64_t>` para detectar valores que no caben
en el `int` de OpenMP antes de convertirlos. No se limita a 1/2/4/8 ni al número
de CPU lógicas. La cantidad de configuraciones debe caber en el índice `int`
de posición; sumas/productos de conteos y capacidades se comprueban.

La lista base es `[S, O(t0), O(t1), …]`, respetando el orden configurado.
Cada ronda desplaza la lista una posición a la izquierda o derecha:

| Ronda (desde cero) | RotateLeft con solicitudes 1, 2, 4 |
|---|---|
| 0 | S, O1, O2, O4 |
| 1 | O1, O2, O4, S |
| 2 | O2, O4, S, O1 |
| 3 | O4, S, O1, O2 |

En un ciclo de M rondas, donde M=1+cantidad de solicitudes, cada configuración
ocupa todas las posiciones exactamente una vez. Con rondas incompletas, cada
posición aparece floor(R/M) o ceil(R/M) veces para cada configuración. Con
una sola ronda no puede equilibrarse el orden. La rotación no elimina efectos
de caché, frecuencia, temperatura, afinidad, carga ajena ni planificación.

Se calcula **un oráculo secuencial de corrección por estudio**, fuera de las
muestras. Después hay W rondas de calentamiento y R rondas medidas. Ambas fases
usan la misma rotación, reiniciando su índice al comenzar las mediciones.
Cada configuración, incluido Secuencial, recibe W calentamientos.

**Por ronda medida se ejecuta Secuencial una sola vez**, junto con una ejecución
por solicitud OpenMP. Ese tiempo secuencial es compartido por todos los puntos
de la ronda: hay R tiempos secuenciales en todo el estudio, nunca K*R.
Las estadísticas finales comparan la mediana de esa serie compartida con la
mediana de cada serie OpenMP; no se calcula una mediana de cocientes por ronda.

La referencia de estado canónica permite verificar una ejecución OpenMP que
preceda al Secuencial de su ronda, sin conservar todos los estados finales ni
repetir cálculos secuenciales por solicitud. Se compara cada Game celda por
celda con el oráculo; el Secuencial medido también se verifica. Si ambos pasan,
la igualdad con la referencia de su ronda queda establecida por transitividad.
Un fallo secuencial invalida los puntos que dependen de la serie compartida,
aunque sus estados OpenMP coincidan con el oráculo. No se declara equivalencia
entre dos estados que hayan fallado la comprobación canónica.

El oráculo cuenta como una ejecución adicional, no como calentamiento ni
medición de referencia para estadísticas; puede afectar cachés, como en Fase 4.
Total de Game construidos: `1 + M*(W+R)`. No se mantiene un Game por solicitud.
Cada ejecución construye desde la misma entrada inmutable, generación cero.

## Auditoría y resultados

`ScalingResult` conserva configuración, identidad de entrada de Fase 4,
estimación de memoria, número de calentamientos terminados y un solo vector
cronológico de `ScalingSample`. No contiene copias completas de tableros.

Cada muestra reutiliza `ExperimentSample`/`BenchmarkSample`, con tiempo original
en milisegundos por lote, generaciones, motor, solicitud, equipo efectivo,
equivalencia con el oráculo, repetición, ronda y posición. Además registra:

- `configurationIndex`: 0 para Secuencial; i+1 para requestedThreads[i].
- `sequentialReferenceIndex`: índice de la única muestra secuencial de su ronda.

`executionIndex` es global: oráculo en 0, M*W calentamientos y después muestras.
Los índices de ronda y repetición empiezan en cero. `positionInRound` está entre
0 y M-1. Configuración, política e índices reconstruyen completamente el orden.
Los calentamientos no se incluyen en muestras ni estadísticas de equipos.

`sample.validForComparison(study.samples[sample.sequentialReferenceIndex])`
comprueba la validez individual y de la referencia de su ronda. El estado de
equivalencia es una observación de la comparación canónica; la validez y las
métricas son derivadas y se mantienen separadas.

`ScalingSummary` conserva estadísticas secuenciales compartidas y un
`ScalingPoint` por solicitud, en el orden de la lista. Cada punto reutiliza
`ExperimentSummary`: estadísticas, equivalencia, validez, rango efectivo,
speedup opcional y eficiencia opcional. Un punto OpenMP fallido conserva sus
tiempos y estadísticas, pero no produce speedup válido; no invalida por sí
solo otros puntos correctos. Un fallo de la serie secuencial compartida sí
invalida todas sus comparaciones. El estado global refleja todos los puntos.

Se mantienen las definiciones de Fases 3/4: desviación poblacional, speedup
mediana S/mediana O, eficiencia speedup/equipo efectivo. Equipos variables dentro
de una muestra o entre rondas conservan su rango, pero no producen eficiencia.
Tiempos no finitos/negativos no se descartan silenciosamente; una serie con
ellos no tiene estadísticas ni métricas válidas. Cero sigue siendo observación
válida, pero una mediana cero impide las métricas por resolución insuficiente.

`summarizeScalingSamples()` comprueba estructura de auditoría/configuración y
reutiliza las estadísticas y reglas de Fase 4 con pares de series temporales.
Los pares temporales copian únicamente metadatos de muestras, no tableros ni
mediciones secuenciales nuevas. Las muestras originales permanecen en su orden.

## Cronómetro, OpenMP y compatibilidad

Se reutiliza `detail::runOnce()` sin modificar su cuerpo ni sus puntos del reloj:
lote de Game::step(), con costes internos y las consultas aprobadas del equipo
efectivo después de cada paso OpenMP. Construcción, generación, validación,
identidad, registro, estadísticas y salida quedan fuera del intervalo.
No hay StepTrace, renderizado ni ejecuciones simultáneas de configuraciones.

Se deshabilita ajuste dinámico durante el estudio; se aplica la solicitud de
cada configuración y se observa el tamaño real del equipo. OMP_THREAD_LIMIT
puede producir varias solicitudes distintas con el mismo equipo efectivo.
No se confunden hilos efectivos con núcleos físicos. El guard compartido restaura
los ICV del contexto llamante al terminar y ante excepciones C++.

`runBenchmark()` permanece intacto; `runExperiment()` comparte validación de
entrada/metadatos extraída y su resumen reserva vectores de tiempos por conteos
exactos. Su procedimiento, alternancia y resultados conservan el significado.
No cambian motores, generador, reglas, instrumentación ni pantallas.

## Presupuesto de memoria

`maxMemoryBytes` predetermina **256 MiB**, política configurable para evitar
reservas accidentales, coherente con la protección del generador. Ahora se
presupuesta el estudio completo, no solo entrada y un Game. Permite 2000×2000
con margen: unos 20 MB de payload de tableros, más registros y estadísticas.
Un valor superior requiere decisión explícita del llamante, no más CPU lógicas.

Antes de reservar resultados o construir un Game, se comprueban:

- Entrada/halo, índices, capacidades, aritmética y metadatos del generador,
  reutilizando la validación experimental; no se modifica el generador.
- `C + 4*P` bytes para tableros: C es la capacidad real del vector prestado
  (incluye capacidad reservada sin usar), P=(ancho+2)*(alto+2).
- M*R registros `ScalingSample`, K puntos `ScalingPoint` y objeto resultado.
- Capacidad del vector de solicitudes del llamante y dos copias de K enteros
  de 64 bits (configuración retornada y validación ordenada temporal).
- Workspace de un punto: 2R `ExperimentSample` y 3R doubles (dos series de
  tiempos reservadas y una copia ordenada para estadística).
- Bytes de versión del generador copiada, incluido terminador cuando corresponda.

La suma es conservadora: incluye espacios de fases que no necesariamente
coinciden. `estimatedPeakBytes` expresa ese payload; **no garantiza RSS máximo**
ni RAM disponible. Excluye sobrecarga del asignador, pilas OpenMP, runtime,
recursos del SO y otros objetos del llamante. La disponibilidad real puede
cambiar; `bad_alloc` se propaga restaurando OpenMP. Solicitudes enormes aunque
representables pueden agotar recursos o ser rechazadas por el runtime; no se
impone un límite artificial basado en CPU visibles.

El estudio mantiene como máximo dos Game simultáneos (oráculo y ejecución
actual). Su coste de tableros es independiente de K. El registro es O(M*R),
los puntos O(K), el workspace adicional O(R). La memoria se comprueba con
aritmética segura, antes de ejecutar, incluyendo límites representables.

## Validación

CTest añade `scaling_study` y `scaling_study_thread_limit` (límite efectivo 1).
Comprueba listas arbitrarias como 1/3/2, rotaciones completas e incompletas,
configuraciones inválidas, estados iniciales, muestras/referencias compartidas,
estadísticas/fórmulas, equipos observados, presupuesto/capacidades y restauración
ante éxito y fallo de asignación. Datos sintéticos comprueban validez, fallos de
referencia, equipos variables y tiempos inválidos, sin umbrales de rendimiento.
Un contador de asignaciones de buffers 5×5 verifica el número real de Game.

Las ejecuciones grandes se hacen separadas de CTest para evitar competencia:

```bash
OMP_DYNAMIC=FALSE OMP_THREAD_LIMIT=8 ./build-release/scaling_study_tests "$PWD" --large
```

Generan una vez cada tablero 1000×1000 y 2000×2000 (densidad 3000, semilla 42)
y ejecutan 2 generaciones, 1 calentamiento y 3 rondas, solicitudes 1/2/4/8.
No se exige speedup > 1 ni un tiempo máximo. Son validaciones funcionales
controladas, no una campaña de rendimiento certificada. CSV/metadatos completos
y gráficas quedan para fases posteriores.

La captura de metadatos y exportación independiente de Fase 6 se documenta en
[EXPERIMENT_CSV.md](EXPERIMENT_CSV.md).

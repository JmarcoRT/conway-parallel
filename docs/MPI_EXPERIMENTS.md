# Infraestructura experimental MPI

La API experimental admite comparación y escalabilidad MPI. Los formularios y
vistas FTXUI todavía no seleccionan ni presentan estos estudios. El benchmark
textual fijo y `openmp_timing` no cambian.

## API y compatibilidad

`ExperimentConfig::participants` permite configurar participantes explícitos:

```cpp
config.participants = {
    {Engine::Sequential, {}, {}},
    {Engine::OpenMP, 4, {}},
    {Engine::MPI, {}, 2},
};
```

Un vector vacío conserva la comparación local existente y sus resultados/CSV v1.
La selección explícita ejecuta un estudio generalizado y devuelve `result.study`.
Es obligatorio seleccionar al menos dos configuraciones; no se añade una muestra
secuencial oculta si el usuario no la selecciona. Siempre existe un oráculo
secuencial para equivalencia, pero su tiempo no sirve de referencia de rendimiento.

`ScalingConfig::parallelEngine = Engine::MPI` usa `requestedProcesses` y produce
una serie secuencial compartida más una serie por P en `result.study`. OpenMP
conserva el ejecutor local y `requestedThreads`. Los nuevos modelos no reutilizan
campos de hilos para procesos. Los adaptadores de compatibilidad conservan sus
campos anteriores vacíos cuando existe `study`; las futuras vistas deben consultar
este campo primero. Las configuraciones y muestras generalizadas se definen en
`include/app/experimental_study.hpp`.

`mpiOptions` permite indicar ejecutables, plazos y argumentos explícitos del
lanzador. Por defecto usa el lanzador configurado por CMake y busca el trabajador
junto al ejecutable que llama a la API. No habilita sobresuscripción. Los plazos
limitan la operación completa: un lote que exceda el plazo falla, no se subdivide
ni se vuelve a intentar. El trabajador mantiene además su plazo absoluto de
30 segundos desde la recepción del frame hasta la respuesta. Se mantienen los límites de 1..1024 procesos,
100000 generaciones por lote y 64 MiB menos 32 bytes para la entrada.

## Orden, recarga y procesos

El orden por ronda es rotativo sobre la lista de configuraciones. Con tres
participantes y rotación izquierda: S/O/M, O/M/S, M/S/O. La política derecha
invierte el desplazamiento. Se conserva el orden real, posición e índice global
(el oráculo es ejecución cero). Con dos participantes ambas rotaciones alternan.
En comparación explícita `ExperimentConfig::rotateRight` selecciona la dirección;
el orden inicial es la lista declarada. `order` conserva su semántica anterior
solo para la comparación local de compatibilidad.

Cada turno MPI crea una sesión nueva. Recarga la misma entrada en generación cero
antes de **cada** calentamiento y muestra, ejecuta un lote, recupera un snapshot y
comprueba equivalencia completa fuera del cronómetro. Tras la muestra, cierra la
sesión y exige confirmación de salida de lanzador/ranks mediante waitpid/pidfds y
retirada del directorio/socket antes de ejecutar el siguiente participante.
Nunca mantiene varias sesiones P, ni una sesión ociosa durante mediciones locales.

Los calentamientos locales siguen siendo por configuración antes de las rondas.
Los MPI son por sesión: W calentamientos para cada muestra, porque cerrar una
sesión pierde el calentamiento del runtime. Con R repeticiones y K configuraciones
MPI se realizan W*R*K calentamientos MPI. El total y el recuento por sesión quedan
registrados. W=0 es válido y significa una muestra en una sesión recién creada.
Ahora también se verifica equivalencia completa de los calentamientos locales;
no se usan como muestras estadísticas.

## Alcance temporal

Protocolo de transporte v2: los pares v1/v2 se rechazan explícitamente.
`RUN_BATCH` contiene N y la generación inicial esperada. El trabajador valida antes
de difundir y no modifica el estado si la petición tiene una generación obsoleta.
Cada rank realiza:

1. Barrera de preparación, **excluida** del tiempo.
2. Inicio con `MPI_Wtime()`.
3. N pasos consecutivos `step_mpi`, sin peticiones IPC por generación.
4. Barrera final, **incluida** en el tiempo.
5. Fin con `MPI_Wtime()`.
6. Reducción MAX de las duraciones, fuera del intervalo.

El resultado principal es el máximo de intervalos locales en ms. Incluye cálculo,
halos y todas las colectivas actuales del motor, incluidas sus validaciones,
reducciones y la barrera final. Todos los ranks cuentan, incluso sin filas.
No compara timestamps absolutos entre ranks y no exige relojes sincronizados.
Excluye arranque, carga, snapshot, reducción de tiempos, IPC de respuesta,
comprobación de equivalencia, estadísticas, captura de contexto y presentación.

Los tiempos auxiliares de cada muestra se miden con `steady_clock` en el cliente:
carga hasta confirmación, solicitud completa de lote y snapshot hasta validación.
La solicitud de lote incluye IPC y engloba el tiempo interno: no es una categoría
aditiva. El arranque (hasta handshake) y cierre se registran por sesión.
Los calentamientos guardan recuento, no series temporales auxiliares.

El tiempo local conserva el alcance previo: lote de `Game::step()` y observación
del equipo efectivo, sin construcción/carga/comparación/render. Su alcance no es
idéntico al máximo MPI: ambos quedan etiquetados, no se presenta un supuesto
tiempo de cálculo puro ni tiempo de CPU.

## Validez, fallos y estadísticas

Se comparan dimensiones, generación, población y todas las celdas finales con el
oráculo. Cada ejecución parte de la entrada original, nunca del lote anterior.
Configuraciones duplicadas, recursos mezclados, límites inseguros o presupuesto
excedido se rechazan antes de lanzar procesos. P mayor que filas es válido.

Un fallo operativo (local o MPI) o calentamiento no equivalente detiene el estudio, sin retry
ni ejecución de participantes posteriores. Conserva muestras y sesiones parciales,
diagnóstico y confirmación de limpieza. Si no puede confirmarse la limpieza,
`cleanupConfirmed=false`; nunca continúa midiendo. `ExperimentExecution` conserva
ese resultado parcial y publica fase Error con su diagnóstico. Un resultado final
no equivalente se conserva como muestra inválida y no permite métricas válidas.

Por configuración se conservan mínimo, máximo, media, mediana y desviación estándar
poblacional. Speedup es mediana secuencial / mediana paralela; exige series completas,
válidas, equivalentes y medianas positivas. La serie secuencial de cada ronda se
almacena una sola vez. Sin participante secuencial no hay speedup/eficiencia.
Eficiencia usa recursos efectivos estables: hilos observados para OpenMP o P
observado para MPI, incluido rank 0 y ranks vacíos. Es eficiencia por proceso,
no por núcleo. Un valor ausente no significa cero. Las muestras inválidas no se
eliminan selectivamente para obtener métricas favorables.

## CSV y metadatos

La API `exportExperimentCsv` conserva los tres archivos. Resultados con `study`
usan esquema v2; los locales anteriores siguen con v1. No se exporta automáticamente
ni se añaden botones FTXUI.

- `samples.csv`: configuración, hilos/procesos separados, recursos observados,
  ronda/posición/ejecución, referencia secuencial compartida, lote, alcance temporal,
  sesión, auxiliares, calentamientos y equivalencia/validez, junto al estado global
  del estudio y la limpieza de la sesión.
- `summary.csv`: motor, recursos solicitados/observados y alcance temporal explícitos,
  estadísticas, speedup/eficiencia y causas de indisponibilidad. Cada fila incluye
  estado completo/incompleto, confirmación de limpieza y diagnóstico; las
  estadísticas parciales no se presentan como un estudio completo.
- `metadata.csv`: entrada/semilla/huella y contexto existente, método v2, políticas,
  presupuestos de payload (no RSS), plazos, ejecutables/argumentos, versión wire,
  partición/ranks vacíos y, por sesión, runtime MPI, slots declarados, arranque,
  cierre, recuento, diagnóstico y limpieza.

La exportación valida el orden y que los resúmenes almacenados concuerden con las
muestras; no ejecuta nuevamente motores. El formato conserva precisión numérica.

## Validación breve

`experimental_mpi` usa tableros pequeños y lotes de tres generaciones: recarga,
equivalencia completa, tres motores, escalabilidad con ranks vacíos, ambas
rotaciones, calentamientos por sesión, series compartidas, CSV v2, fallos reales
de rank, plazos y limpieza. Un lanzador de prueba rechaza workers anteriores vivos
al iniciar otra sesión. `mpi_batch_metrics` comprueba el MAX colectivo con valores
sintéticos conocidos por rank, sin exigir aceleración ni duración mínima.

# Metadatos y exportación CSV — Fase 6

API en `app/experiment_metadata.hpp` y `app/experiment_csv.hpp`, target
`conway_experiment_export`. Consume resultados de comparación y escalabilidad,
sin FTXUI, generación, llamadas a motores ni recálculo de estadísticas/métricas.
No modifica `runBenchmark()`, `runExperiment()` ni `runScalingStudy()`.

```cpp
#include "app/experiment_csv.hpp"

const auto board = conway::input::generateInput({1000, 1000, 3000, 42});
// Capturar inmediatamente antes de ejecutar; conservar este mismo contexto.
const auto context = conway::app::captureExperimentContext({
    "study-001", std::nullopt, {{"operator_note", "ensayo exploratorio"}}});
const auto result = conway::app::runScalingStudy(board, {20, 1, 5, {1, 2, 4, 8}});
const auto paths = conway::app::exportExperimentCsv(result, context, "/tmp/study-001");
```

La carpeta de destino debe ser **nueva** y su padre debe existir. Se crean
`samples.csv`, `summary.csv` y `metadata.csv`. El identificador lo asigna el
llamante: no se usa como ruta ni se garantiza unicidad entre directorios distintos.
Para una entrada `.txt`, proporcionar `MetadataOptions::patternName`, por ejemplo
`captureExperimentContext({"pattern-study", "patterns/blinker.txt", {}})`.

`describeExperiment(context, result)` permite inspeccionar el modelo sin escribir.
`writeExperimentCsv(result, context, samplesStream, summaryStream, metadataStream)`
permite escribir a streams. Ambas APIs admiten comparación y escalabilidad.
Los metadatos de configuración/entrada siempre se construyen desde el resultado
suministrado, para evitar mezclar una descripción de otro tipo de estudio.

## Procedencia y disponibilidad de metadatos

Cada `MetadataField` conserva clave, valor opcional, fuente y detalle. Fuentes:

| Fuente CSV | Significado |
|---|---|
| runtime_observed | Capturado automáticamente fuera del benchmark |
| build_captured | Snapshot de build o macro del compilador, con alcance documentado |
| result | Datos/configuración del resultado, o versión del contrato |
| user | Identificador, nombre de patrón o campos aportados por el usuario |
| unavailable | Valor ausente y motivo; nunca un cero inventado |

Campos generales: `study.id`, `experiment.type` (comparison/scaling),
`protocol.version` (conway-experimental-v1), `csv.schema_version` (1), `csv.files`.
El versionado del protocolo y el del esquema CSV son independientes.

Entrada: dimensiones, conteo vivo inicial, fingerprint FNV-1a de 16 dígitos hex,
algoritmo de fingerprint y procedencia. Para entradas generadas, también semilla,
densidad solicitada por 10000, fracción observada y versión del generador. Para
`InputData` sin etiqueta, la procedencia queda no disponible: no se inventa que
provenga de un patrón. La etiqueta de patrón es una afirmación del llamante.

Ejecución: generaciones, calentamientos por configuración, repeticiones,
calentamientos terminados, muestras medidas, lista de solicitudes separada por
punto y coma, política de orden y ubicación de la secuencia efectiva en
`samples.csv`. Escalabilidad conserva presupuesto y estimación de memoria.

`execution.context_captured_at_utc` es la hora UTC de **captura del contexto**,
con precisión de segundos. Debe capturarse antes de ejecutar. No se etiqueta
como un inicio interno observado: `execution.internal_start_at_utc` permanece
no disponible porque los procedimientos existentes no registran ese instante.
No debe capturarse un contexto nuevo al exportar resultados antiguos. El
llamante puede aportar una hora conocida como campo `user.*`, claramente manual.

Entorno: uname (SO/kernel en plataformas POSIX), procesadores lógicos disponibles
según `omp_get_num_procs()`, ICV observados y variables OMP_NUM_THREADS,
OMP_DYNAMIC, OMP_THREAD_LIMIT, OMP_PROC_BIND, OMP_PLACES, OMP_SCHEDULE,
OMP_WAIT_POLICY, OMP_MAX_ACTIVE_LEVELS, OMP_STACKSIZE, OMP_DISPLAY_ENV,
GOMP_CPU_AFFINITY y KMP_AFFINITY. Una variable vacía está disponible con valor
vacío; una variable no definida tiene fuente unavailable y motivo.
No se infieren núcleos físicos, modelo de CPU ni topología del host WSL.

`openmp.specification_date` corresponde a `_OPENMP`, no a una versión de libgomp
u otra biblioteca cargada. `openmp.runtime_version` queda no disponible porque
la API estándar no la expone. La captura POSIX fallida o no soportada también
se marca explícitamente. Los campos del usuario llevan prefijo `user.` y no
sobrescriben datos automáticos; claves vacías o duplicadas se rechazan.

## Snapshot controlado de compilación

CMake exporta `compile_commands.json` y ejecuta `cmake/CaptureExperimentBuild.cmake`
al construir el target de exportación. El script obtiene identidad/versión/ruta
del compilador, configuración del build, comandos completos generados para los
objetos Secuencial, OpenMP, benchmark, comparación y escalabilidad, y Git HEAD y
estado clean/dirty si Git está disponible. Incluye fecha de captura del snapshot.

Los comandos contienen los flags del target experimental, sin deducirlos solo
del nombre Release. En desarrollo, un tipo CMake vacío se registra como
`unspecified`, no como Debug. En generadores sin compile_commands, los comandos
quedan no disponibles. Los comandos son evidencia de las recetas generadas por
CMake; no certifican cambios ocultos introducidos por launchers externos.

El snapshot describe el build, **no el estado vivo del repositorio al ejecutar**.
Un commit posterior o cambios posteriores al build no se consultan desde el
benchmark. Volver a construir actualiza el snapshot. Un build con modificaciones
registra honestamente el commit base y dirty; no afirma corresponder a un commit
limpio. La captura no falla por ausencia de Git y no interpreta macros del
compilador como prueba de una versión de runtime.

Fuera del target CMake controlado (por ejemplo compilación manual con UBSan),
los comandos/commit/tipo quedan no disponibles; no se reutiliza un snapshot
Release para describir un binario con flags de sanitización. No se añaden
herramientas externas durante captura en runtime, mediciones ni exportación.

## CSV v1

UTF-8 sin BOM, separador coma, registros CRLF y escape estándar de comillas
mediante duplicación. Comas, comillas y saltos de línea quedan entre comillas.
Se preservan LF/CRLF internos. Se rechazan UTF-8 mal formado y NUL; no se
transcodifican cadenas de una página de códigos local.

Los doubles usan `to_chars`, punto decimal y `max_digits10`: se puede reconstruir
el valor binario original leyendo como double. No hay redondeo de presentación
ni dependencia de locale. Los tiempos originales inválidos se conservan como
negativos o tokens `nan`, `inf`, `-inf`, siempre acompañados de estado inválido.

Encabezado exacto de **samples.csv**:

```text
schema_version,study_id,experiment_type,configuration_id,engine,requested_threads,effective_threads_min,effective_threads_max,repetition,round,position,execution_order,sequential_reference_sample,generations,elapsed_ms,elapsed_ms_status,equivalent,valid,invalid_reason
```

Una fila por medición, en el orden original. `configuration_id` es sequential
o openmp:N. Repetición/ronda/posición y referencia de muestra empiezan en cero;
`execution_order` es el índice global que incluye el prefijo del oráculo y
calentamientos. `sequential_reference_sample` apunta al índice de fila de datos
de la muestra secuencial de su ronda, no al índice global. El Secuencial
compartido se escribe una sola vez, aunque lo referencien varias solicitudes.
`equivalent` conserva la comparación canónica; `valid` aplica los predicados
existentes (en escalabilidad también la referencia compartida). Los motivos
incluyen non_finite_time, negative_time, non_equivalent,
invalid_execution_metadata e invalid_sequential_reference.

Encabezado exacto de **summary.csv**:

```text
schema_version,study_id,experiment_type,configuration_id,engine,requested_threads,effective_threads_min,effective_threads_max,sample_count,min_ms,max_ms,mean_ms,median_ms,stddev_population_ms,statistics_status,speedup,speedup_status,efficiency,efficiency_status,equivalent,valid,invalid_reason
```

Una fila Secuencial seguida de una fila por configuración OpenMP, en orden de
la lista solicitada. Estadísticas originales: milisegundos por lote, desviación
poblacional. No se reejecuta ni recalcula el benchmark. La fila Secuencial no
inventa speedup/eficiencia de 1: ambos quedan vacíos con estado not_applicable.

Opcionales ausentes se escriben vacíos. `statistics_status` indica available o
unavailable_in_result. Estados de speedup: available, non_equivalent,
invalid_comparison, statistics_unavailable, insufficient_clock_resolution o
unrepresentable_or_unavailable. Eficiencia añade variable_effective_threads,
invalid_effective_threads o speedup_unavailable:motivo. No se convierte un dato
no disponible en cero. Los booleanos se escriben true/false.

Encabezado exacto de **metadata.csv**:

```text
study_id,key,value,source,detail
```

Los tres archivos comparten el identificador, aunque contenga caracteres que
requieran escape CSV. Las claves son únicas; el orden de filas/columnas es
estable para un mismo contexto y resultado. El orden efectivo completo está
en las filas samples.csv y se relaciona desde metadata.csv, sin duplicarlo.

## Errores y límites

El exportador verifica estructura, índices, coherencia de equivalencia/equipos
y validez de métricas almacenadas; rechaza resúmenes obsoletos contradictorios.
No verifica fórmulas recalculándolas: el llamante debe conservar juntos muestras
y resumen producido por la API. Los resultados públicos siguen siendo mutables.

Se adquiere el destino mediante creación exclusiva de un directorio nuevo.
Se rechaza cualquier destino existente; no se sobrescriben resultados previos.
Se comprueban apertura, escritura, flush y cierre. Ante fallo se intenta retirar
solo los tres archivos propios y el directorio vacío; si también falla la
limpieza, puede quedar una salida parcial. No es una transacción durable ante
caídas del proceso/energía, ni una protección frente a modificaciones concurrentes
del destino por el propietario. El usuario debe verificar los tres archivos.
Los streams del llamante pueden quedar parcialmente escritos ante error.

No se añaden dependencias de ejecución externas. Las pruebas usan el módulo
csv estándar de Python ya requerido por CTest. Cubren ambas APIs, precisión y
locale, filas/orden/referencia compartida, procedencia, valores inválidos,
permisos, escape, lectura/escritura, no sobrescritura y fallos de streams.
Los fixtures temporales con tiempos sintéticos están identificados como tales;
no se incorporan mediciones oficiales ni se ejecutan campañas extensas.

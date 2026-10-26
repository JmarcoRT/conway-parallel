# Arquitectura vigente

Este documento describe el código implementado. MPI está integrado en simulación
local mediante procesos separados; Pthreads continúa pendiente. El [índice](README.md) separa contratos y registros históricos.

## Capas y dependencias

| Capa | Responsabilidad | Dependencias propias |
|---|---|---|
| input | Parseo, validación representable y generación determinista | C++ estándar |
| core | GameState, Game, B3/S23 y motores | InputData; runtimes solo en motores específicos |
| app: sesiones | Simulación local/MPI y reproducción didáctica local | Game, trazas, cliente MPI en worker |
| app: experimentos | Medición, muestras, estadísticas y escalabilidad | Game, validación, OpenMP, cliente MPI externo |
| app: exportación | Contexto, metadatos y CSV | Resultados experimentales |
| app: FTXUI | Navegación, formularios, workers y vistas | Servicios de aplicación y FTXUI |
| ui | Renderizadores textuales, layout y demos | Estado del juego |
| mpi | Partición, evolución, transporte y supervisión local | GameState, InputData, POSIX; MPI en el trabajador |

`src/main.cpp` llama a `app::run()`. Esta función selecciona FTXUI cuando entrada
y salida son TTY, y conserva el recorrido textual para pipes. La UI no contiene
reglas B3/S23. Los experimentos no dependen de FTXUI; MPI se ejecuta en un
trabajador externo, mediante el cliente Unix.

`SimulationSession` usa el cliente de [MPI_SESSION.md](MPI_SESSION.md) en un
worker exclusivo. `mpi::Client` lanza `conway_mpi_worker` por sesión y usa un
socket Unix con rank 0. La UI publica snapshots confirmados mediante eventos;
las copias de `Game` nunca poseen un comunicador ni llaman a MPI.

## Entrada y memoria

`loadInput()` lee alto/ancho y filas binarias. `InputData` contiene exclusivamente
el tablero lógico row-major, sin halo. `validated_dimensions.hpp` comprueba
positividad, aritmética de dimensiones/halo, capacidad del vector y límite firmado
del bucle OpenMP antes de reservar. `Game` exige cantidad exacta y células 0/1
antes de construir buffers. No se impone aquí un presupuesto arbitrario de RAM;
`bad_alloc` sigue siendo posible para tamaños representables.

`generateInput()` es independiente y conserva su algoritmo versionado MT19937
con rechazo, recorrido por filas y presupuesto configurable. Véase
[GENERATED_INPUT.md](GENERATED_INPUT.md).

`GameState` conserva width, height, stride=width+2, dos vectores de bytes,
generation y aliveCount. Cada vector tiene `(width+2)*(height+2)` posiciones.
El halo exterior se inicializa a cero y no evoluciona: borde muerto fijo, no
periódico. Una generación lee solo current y escribe las células lógicas de next;
ambos buffers conservan el halo cero. El swap se realiza una vez al terminar.

## Reglas y motores

`next_cell_state()` aplica B3/S23 a ocho vecinos. No se duplica en los motores.
`Game::step()` despacha según `Engine::Sequential` o `Engine::OpenMP`.
La evolución MPI independiente usa `core::step_mpi` sobre franjas locales y no
forma parte de este despacho. Intercambia halos con vecinos no vacíos, reutiliza
el recorrido secuencial y reduce la población global. Véase [MPI_PROTOTYPE.md](MPI_PROTOTYPE.md).

Secuencial recorre filas y columnas; OpenMP reparte filas completas con
`schedule(static)`. Todos leen current; cada hilo escribe filas disjuntas de next.
La población usa reducción y el estado se actualiza después de sincronizar.
El número solicitado pertenece al llamador; el efectivo se observa dentro del
team. `last_openmp_team_size()` conserva esa observación en el hilo llamante.

GameState y la regla no dependen de MPI/OpenMP. La implementación específica del
motor OpenMP sí incluye `omp.h`; no debe confundirse con una dependencia de los
tipos compartidos. No se deduce una asignación observada a partir de hilos pedidos.

## Simulación e instrumentación

`SimulationSession` mantiene entrada inicial, snapshot Game, motor y reproducción.
El reinicio restaura la referencia inicial y pausa. Configurar motor/P conserva
el estado confirmado y dicha referencia. MPI ejecuta cliente/cierre fuera de UI
y entrega un snapshot por operación; no acumula avances. La presentación NEXT
local usa una copia precomputada fuera del render; MPI solo muestra CURRENT.

StepTrace es opcional: sin sink no se capturan relojes ni registros didácticos.
El sink se invoca una vez en el hilo original, después del trabajo paralelo.
Los trabajadores sin filas no aparecen, aunque formen parte del equipo efectivo.
Los intervalos históricos no tienen el mismo alcance en ambos motores; OpenMP
incluye espera en la barrera del for. Contrato exacto: [STEP_TRACE.md](STEP_TRACE.md).

`DidacticController` prepara PRE/POST y traza en un worker, publica bajo mutex y
reproduce progreso relativo. La lista por thread_id no informa orden temporal.
No se reconstruye una línea temporal real entre hilos.

## Medición y resultados

Hay dos procedimientos distintos:

- `runBenchmark()`: API histórica, serie secuencial seguida de OpenMP, un
  calentamiento por motor y campos compatibles con consola. El resumen histórico
  de equipos incluye calentamiento.
- `runExperiment()`: configuración explícita, rondas alternadas, muestras
  cronológicas y métricas opcionales con validez comprobada.
- `runScalingStudy()`: lista de solicitudes, rotación reproducible y una serie
  secuencial medida compartida. No repite runExperiment por configuración.

Los tres reutilizan `benchmark_detail.hpp`. Se construye Game antes del reloj;
el intervalo cubre el lote de pasos sin sink y observaciones de equipos.
Generación, identidad, comparación final, estadísticas y presentación quedan
fuera. Un oráculo de corrección no es una muestra medida.

Las estadísticas conservan tiempos originales: mínimo, máximo, mediana, media
y desviación estándar poblacional. Vacío se expresa con optional; cero puede ser
observación válida, pero mediana cero impide métricas configurables. Speedup usa
medianas; eficiencia usa equipo efectivo estable, nunca la solicitud como sustituto.
Ver [protocolo](BENCHMARK_PROTOCOL.md), [comparación](EXPERIMENT_RUNNER.md) y
[escalabilidad](SCALING_STUDY.md).

## Ejecución y presentación FTXUI

`ExperimentFormView` mantiene borradores y llama a validación pura.
`ExperimentExecution` congela solicitud, prepara entrada una vez y captura contexto
en su worker persistente. Publica snapshots y resultados inmutables compartidos
bajo mutex; notifica a UI por PostEvent. Excepciones conservan el resultado anterior.
No hay cancelación forzada; la salida activa espera sin bloquear el bucle UI.

`comparison_view` y `scaling_view` consumen resultados existentes. Redondean solo
la pantalla; no calculan estadísticas experimentales ni modifican muestras.
Resumen, Muestras y Método comparten convenciones, navegación y desplazamiento.
La tabla de escalabilidad se ordena para lectura; muestras y CSV conservan adquisición.

`DifferentialOutputBuffer` compara filas rasterizadas y escribe las modificadas.
Un cambio de tamaño permite repintado completo. No hay render periódico en pausa.
La medición no incluye render, aunque UI/SO pueden competir por recursos fuera
del intervalo. Más detalles: [UI.md](UI.md) y
[FTXUI_EXPERIMENT_EXECUTION.md](FTXUI_EXPERIMENT_EXECUTION.md).

## Metadatos y CSV

`captureExperimentContext()` registra observaciones previas; CMake incorpora un
snapshot de compilación/Git, no consulta Git dentro de la medición.
El resultado y su contexto se conservan juntos. Exportación valida y serializa
samples.csv, summary.csv y metadata.csv, sin reejecutar motores ni redondear.
Existe como API; no hay botón FTXUI. Véase [EXPERIMENT_CSV.md](EXPERIMENT_CSV.md).

## MPI, Pthreads y límites actuales

MPI evoluciona con halos, se compara contra secuencial y se integra en la
simulación mediante un trabajador externo. Una partición local puede tener cero
filas; no se confunde con Game global no vacío. El modo didáctico MPI sigue pendiente. La API experimental admite MPI con
lotes temporizados; sus formularios y vistas todavía no lo seleccionan.
Véase [MPI_EXPERIMENTS.md](MPI_EXPERIMENTS.md). Véanse [evolución](MPI_PROTOTYPE.md) y [sesiones](MPI_SESSION.md).
Pthreads no tiene motor implementado.

No hay historial persistente, publicación automática ni campaña de rendimiento
certificada. Los presupuestos estiman payload, no RSS. Rutas POSIX y preset
G++ 14 hacen de Linux/WSL el entorno soportado de referencia.

## Contratos de integración

Conservar una sola representación, una regla compartida, validación previa a
reservas y sincronización antes del swap. No introducir render ni E/S en motores
o intervalos experimentales. Cambios públicos requieren coordinación y pruebas.
No eliminar evidencia histórica ni mezclar tiempos de metodologías diferentes.

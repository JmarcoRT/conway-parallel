# Controlador didáctico: captura y reproducción

El controlador y las vistas FTXUI admiten Secuencial, OpenMP y MPI. Los tres
motores comparten reloj y controles, conservando sus explicaciones específicas.

## Vistas MPI

Selecciona MPI y P en Motores, carga una entrada y entra en Visualización
didáctica desde el menú principal. La célula capturada está en la última fila
de R0 y la columna central: con P>1 permite explicar una frontera entre franjas.
Las coordenadas de filas/columnas mostradas tienen base cero.

La cabecera distingue generación visible y capturada, etapa, reproducción y
velocidad. El aviso permanente indica «Captura MPI real; orden de presentación
didáctico». Reparto actual es contexto, no un Scatterv nuevo en cada paso.

V alterna Global/Local/Explicación; [ y ] seleccionan cualquier rank, incluidos
los vacíos. En terminal amplia, Global y Local muestran el tablero global y
paneles separados para tantos ranks como quepan, agrupados alrededor del rank
seleccionado. Explicación conserva todo el ancho. En terminal pequeña cada vista
ocupa el ancho disponible. Flechas arriba/abajo, PgUp/PgDn y Home/End recorren
líneas físicas envueltas, con indicador de posición; el estado y la ayuda quedan
fuera del contenido desplazable. Cambiar vista, rank o tamaño no toca el controlador.

Global reúne exclusivamente filas propias capturadas. Local etiqueta filas
propias, halos y bordes laterales; distingue CURRENT antes de comunicación,
CURRENT con halos válidos y NEXT calculado. En Cambio de buffers muestra CURRENT
nuevo y el buffer anterior reutilizado como NEXT. Los halos de la generación
resultante se marcan NO vigentes. Estos buffers son snapshots capturados, no
memoria compartida ni una ejecución en vivo. Explicación muestra mensajes y
valores reales, MPI_PROC_NULL, procedencia de cada vecino y evaluación B3/S23.
Rank 0 también calcula; ranks vacíos no intercambian ni evalúan células.

Espacio pausa/reanuda, N avanza una etapa en pausa, +/- cambia velocidad, R
reinicia y Esc cierra asincrónicamente. Los errores iniciales y posteriores se
presentan con diagnóstico desplazable sin reintento automático.

## Dos responsabilidades separadas

`DidacticController` pertenece al hilo UI. Un trabajador persistente posee
`DidacticBackend`, el Game local o el cliente MPI. Solo el trabajador calcula,
lanza procesos, carga, captura, reinicia o cierra la sesión. Entrega una captura
inmutable por buzón; `poll()` la acepta en el hilo UI. `notify` se ejecuta en el
trabajador y debe limitarse a publicar un evento, nunca renderizar ni manipular
estado UI. FTXUI recibe la notificación sin necesitar otra pulsación.

Hay un único comando en curso. Los ticks y el paso manual no acumulan capturas
mientras se espera una respuesta. Un reinicio/cierre pendiente sustituye al
anterior; los comandos llevan un epoch. Reiniciar o salir lo incrementa e
invalida inmediatamente la captura visible. Una respuesta antigua no puede
publicar datos ni reanudar la animación. La limpieza MPI no confirmada NO se
oculta por ser una respuesta antigua: detiene el flujo y mantiene la reserva.

`startPreparation`, `resetToInitial`, `close`, controles, tick y poll no unen
hilos ni esperan RPC. `waitUntilReady`, `prepareSynchronously` y `joinWorker`
son helpers bloqueantes para pruebas, nunca acciones UI. El destructor une el
trabajador como último recurso de cierre: la aplicación usa close/poll y retiene
el objeto hasta que closed() sea verdadero antes de salir. El temporizador UI
es cancelable, para que reemplazarlo no espere a un sleep pendiente.

## Tiempo y etapas

El reloj es exclusivamente de reproducción. El valor inicial es 2 segundos por
etapa a 1×. `setStageDuration` permite un valor positivo de hasta una hora y
conserva el progreso proporcional de la etapa; +/- recorre multiplicadores de
velocidad. Cambiar velocidad no cambia etapa ni recalcula una captura. Las
mediciones de StepTrace/MPI no fijan la duración de la animación.

Etapas locales:

1. Trabajo registrado: rangos de filas y registros realmente observados.
2. Resultado de la generación capturada.

La revelación de filas es una animación uniforme sobre cobertura observada,
no un orden de células capturado ni una duración de trabajo útil. No se inventan
mensajes, cálculos individuales o fases de comunicación para los motores locales.

Etapas MPI:

1. Estado local y **Reparto actual**, contexto de propiedad, sin evento Scatterv.
2. Primer intercambio registrado (orden local de llamadas).
3. Segundo intercambio registrado (orden local de llamadas).
4. Célula seleccionada y evaluación capturada.
5. Buffers y resultado local capturados.
6. Resultado de la generación.

Estos son pasos de explicación, no una cronología global ni ejecución en vivo.
Los datos MPI proceden exclusivamente de TRACE_STEP/TRACE_RESULT; los Game usados
como snapshots globales se construyen reuniendo las filas capturadas y NUNCA
se evolucionan secuencialmente para producir el resultado MPI. La traza MPI se
conserva separada de StepTrace y de los trabajadores/hilos locales.

## Reproducción y generaciones

Al iniciar una preparación se activa la intención de reproducción automática.
Espacio la alterna también mientras la captura está pendiente. Esa captura puede
terminar, pero aceptar su resultado conserva la pausa que haya solicitado el
usuario. Reanudar durante captura tampoco envía otra petición.

Cada tick consume tiempo real escalado solamente si hay captura lista y se está
reproduciendo. Al cambiar de etapa se desecha el exceso del tick: una UI retrasada
no salta etapas ni acumula generaciones. El tiempo de espera de captura no se
carga a la animación recién recibida. Al terminar la última etapa se solicita
UNA captura de la generación siguiente; su publicación empieza la etapa inicial
sin cambiar la intención de reproducción.

N avanza exactamente una etapa cuando está pausado. Al final solicita la próxima
captura y permanece pausado. `advanceGeneration` queda como acción explícita de
compatibilidad para consumidores antiguos, solo en pausa; la UI utiliza stepStage.
R vuelve a la entrada inicial, conservando pausa/reproducción y velocidad. Un
reset MPI cierra el trabajo anterior con limpieza confirmada antes de crear el
siguiente y cargar generación 0. Entre generaciones normales reutiliza una sola
sesión, sin recargar la entrada y sin repetir un paso calculado.

Campos diferentes de DidacticFrame:

- `current_generation` / `next_generation`: extremos de la captura visible.
- `captured_generation`: última generación calculada y aceptada por el controlador.
- `viewed_generation`: generación anterior durante la explicación y generación
  siguiente en la etapa de resultado.

Mientras se prepara la siguiente generación se conserva la captura anterior
para consumidores que quieran mostrarla. Un reset/cierre la invalida. Las vistas
locales actuales mantienen su panel de preparación durante esa espera.

## Fallos y exclusión de sesiones

Un error detiene reproducción, conserva diagnóstico y último resultado aceptado;
no reintenta capturas ni cambia de motor. El cierre conserva el diagnóstico.
Solo una acción explícita de reinicio/inicio puede recuperar. Los fallos de una
petición ya invalidada se descartan si su limpieza está confirmada; una limpieza
incierta prevalece sobre el epoch y bloquea cualquier reemplazo.

ownsMPISession() incluye creación reservada, sesión lista, cierre pendiente y
limpieza incierta. La UI impide los dos experimentos configurables y el benchmark
histórico mientras esa reserva exista; tampoco abre otra visualización. A la
inversa, una sesión MPI normal abierta o una transición pendiente impide entrar
en didáctica. Los cierres de capturas locales pendientes también impiden empezar
mediciones. Salir de didáctica solicita cierre asíncrono, no deja ranks ociosos
para medir después. Salir de la aplicación espera ambas sesiones y el experimento.

Los límites de captura y el protocolo se mantienen en [MPI_TRACE.md](MPI_TRACE.md).
No cambia la metodología experimental, ADVANCE ni RUN_BATCH.

## Pruebas

`didactic_session` usa reloj simulado y un backend bloqueable alrededor de los
motores locales reales. Cubre pausa durante captura, una petición, transiciones,
velocidad, reinicios coalescidos, resultados obsoletos, cierre y errores de limpieza.
Conserva cobertura/equivalencia secuencial y OpenMP con 1/2/4/8 hilos.

`didactic_mpi` usa procesos reales P=1/2/4, entrada pequeña, particiones desiguales
y vacías. Comprueba trazas MPI conservadas, generaciones, equivalencia, etapas,
pausa/reset/cierre y el worker de fallo posterior al cálculo. `step_trace` mantiene
el contrato de instrumentación local y `didactic_layout` la geometría existente.

`ftxui_didactic_controller_pty` comprueba notificación sin otra tecla, reloj lento,
pausa, N, velocidad, reinicio y salida en Secuencial (120×40 y 50×24) y OpenMP
(80×24). Inyecta además una sesión MPI real para comprobar los bloqueos.

`ftxui_didactic_mpi_views_pty` entra por el menú con P=2 y un blinker que cruza
la partición. Comprueba contenido de halos, evaluación de frontera, navegación
sin recaptura, desplazamiento físico hasta la última línea, reproducción entre
generaciones, pausa, paso, reinicio, fallo real de trabajador y cierre. Redimensiona
120×40 → 50×24 → 40×18 → 160×45. Otro recorrido consulta un rank vacío y muestra
el diagnóstico de una primera captura rechazada sin lanzar procesos.
`didactic_mpi` verifica además que las filas de presentación conservan los halos,
roles de buffers, población y estados vacíos de las capturas reales.

Las regresiones PTY de accesibilidad/redimensionado pausan explícitamente la
reproducción antes de verificar las filas. Ninguna prueba exige aceleración.

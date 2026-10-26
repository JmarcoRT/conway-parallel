# Captura didáctica MPI: contrato y transporte

El contrato, instrumentación y transporte de una generación MPI están implementados
y consumidos por [DidacticController](DIDACTIC_CONTROLLER.md). El controlador
reproduce etapas automáticamente con reloj lento, pausa/reanudación y paso manual.
Las vistas MPI de FTXUI consumen estos datos reales sin sustituir MPI por un motor
local; véase [DIDACTIC_CONTROLLER.md](DIDACTIC_CONTROLLER.md).

## Datos y significado

`include/mpi/step_trace.hpp` define `TraceRequest`, `RankStepTrace` y
`MpiStepTrace`. Las coordenadas globales son base cero; los buffers locales
incluyen halos y usan stride `width+2`, con filas propias locales 1..rowCount.
Las columnas 0 y width+1 son fronteras globales muertas.

La partición es **Reparto actual**: describe la propiedad de las filas. Una
captura NO contiene un evento Scatterv, ni implica redistribución por generación.
La distribución ocurre al cargar el tablero; TRACE_STEP utiliza el estado local
que conserva cada rank de la generación anterior.

Cada registro de rank contiene:

- Identidad, rango propio, generaciones antes/después y población local.
- `currentBefore`: copia del buffer real ANTES de limpiar halos. Sus halos pueden
  ser antiguos; la UI los muestra NO vigentes hasta la siguiente actualización.
- `currentWithHalos`: buffer de lectura real después de los dos Sendrecv.
- `currentAfter`: buffer real de salida después del cálculo y del swap. Sus
  filas propias pertenecen a g+1; sus halos NO se declaran válidos para g+1.
- `buffersSwapped`: comprobación de las identidades de ambos buffers. El antiguo
  CURRENT, ahora NEXT, conserva exactamente `currentWithHalos`; no se duplica
  ese snapshot. El cálculo lee g y escribe el otro buffer, nunca actualiza
  células propias in situ.
- Dos intercambios en orden LOCAL de llamada (ninguno si no tiene filas), con
  tag, destino de envío, origen de recepción, filas globales/locales y bytes
  realmente enviados/recibidos. `noPeer=-1` representa MPI_PROC_NULL en el
  contrato portable: no hay fila ni payload de mensaje para ese extremo.
- Solo el propietario de la célula seleccionada contiene `CellProbe`, con
  estado anterior, ocho vecinos en orden NW,N,NE,W,E,SW,S,SE, su suma y el
  resultado escrito por la evaluación real de B3/S23.

Las filas recibidas se copian después de Sendrecv; se verifica MPI_Status y
MPI_Get_count para cada recepción real. Las filas enviadas se copian del buffer
que se pasa a la llamada. El borde global se limpia antes de comunicar.
El probe se rellena en el recorrido local mediante una sobrecarga instrumentada
de la MISMA evaluación compartida de la regla: no se ejecuta una regla duplicada,
ni una evolución global secuencial para construir el resultado MPI.

Todos los ranks figuran, incluido rank 0 y los vacíos. Rank 0 calcula su franja y
además atiende IPC/recolección. Los vacíos no tienen cálculo ni mensajes de halo,
pero participan en validaciones y colectivas, avanzan generación y contribuyen
cero a la población global. Los globales antes/después son sumas de filas propias,
no de halos. La recolección de trazas añade colectivas exclusivas de captura.

La lista de ranks está ordenada por identificador. Los dos intercambios indican
orden local de programa y dependencias de datos, NO una cronología global
observada ni simultaneidad entre procesos. No se capturan relojes globales ni se
usan duraciones de captura como medidas de rendimiento. No hay sleeps, pausas,
animación, reintentos ni peticiones por evento dentro del motor.

## Límites y errores

Política independiente de LOAD/ADVANCE/RUN_BATCH:

- Tablero de hasta 16384 células, dimensiones positivas, conteos representables.
- P entre 1 y 128; se incluyen los registros y halos de los ranks vacíos.
- Resultado completo de hasta 8 MiB. Se calcula su tamaño EXACTO antes de
  reservar buffers; el coste de halos puede alcanzar el límite aun si el tablero
  cabe en el límite de células. No hay truncamiento de ranks, filas ni mensajes.
- Célula dentro del tablero y generación esperada menor que SIZE_MAX.
- Exactamente una generación por petición; sin selección implícita de otra célula.

El límite de 8 MiB es de serialización, no de memoria RSS. Las copias locales,
validación y recolección consumen memoria adicional acotada por esas dimensiones;
no incluye el runtime MPI. No se reserva una copia global del límite en cada
rank: los buffers locales dependen de su partición y rank 0 recibe el resultado.

La preparación se valida también en step_mpi antes de mutar el estado: límites,
partición, identidad de generación, buffers, propietario/coordenadas de la célula
y todos los metadatos de mensajes. El rechazo se acuerda colectivamente; una
preparación inválida en un rank conserva los buffers de todos. La validación
válida no reserva nuevos snapshots. Cada rank conserva la selección recibida,
derivada de la cabecera global al decodificar, sin duplicarla en el formato wire.
Se exige el mismo request y captura habilitada en todos los ranks de esa llamada.

El cliente valida sus límites antes de enviar; un error local de argumentos no
avanza el estado ni invalida una sesión sana. El trabajador valida generación,
selección y límites antes de difundir la operación. Una petición semánticamente
inválida recibe ERROR y no modifica la generación ni sus buffers. El cliente
aplica su política existente: ante error remoto/protocolo/plazo invalida el
trabajo, conserva el diagnóstico y entrega ClientError con cleanupConfirmed.

Los ranks acuerdan la disponibilidad de los buffers de captura antes de avanzar.
También acuerdan el éxito de codificación antes de Gatherv. Los fallos de captura
no producen una respuesta parcial aceptable: un fallo interno aborta el trabajo
y el cliente supervisa y limpia. No se reintenta el paso, que podría ya haberse
calculado. Los errores de cabecera o versión cierran la conexión. Una incompatibilidad
identifica versión esperada y recibida; el worker la conserva en stderr para
que la supervisión no la reduzca a una desconexión sin explicación.

## Protocolo v3

TRACE_STEP (tipo 11): tres u64 big endian: generación esperada, fila global,
columna global. Tamaño exacto: 24 bytes. Requiere LOAD previo.

TRACE_RESULT (tipo 12): cabecera de nueve u64 (72 bytes): ancho, alto, generación
antes, generación después, población antes, población después, fila seleccionada,
columna seleccionada y P. Sigue un registro por rank en orden de identificador.

Registro por rank:

1. Ocho u64: rank, primera fila global, cantidad de filas, generación antes,
   generación después, población antes, población después y swap confirmado (1).
2. Tres buffers binarios de `(width+2)*(rowCount+2)` bytes en el orden descrito.
3. Para un rank no vacío, dos registros Sendrecv. Cada uno contiene seis u64:
   tag, destino+1, origen+1, fila enviada+1, fila recibida+1 y fila local de
   recepción; cero representa ausencia de peer/fila en los campos opcionales.
   Siguen width bytes si hay envío real y width si hay recepción real.
4. Solo para el propietario seleccionado, tres u64: fila local, columna local y
   probe capturado (1); once bytes: estado anterior, ocho vecinos, suma y resultado.

No se serializan punteros ni estructuras nativas. Las longitudes internas se
calculan de dimensiones, partición y selección; no hay prefijos que permitan
solicitar asignaciones arbitrarias. Tras leer los 72 bytes fijos se comprueban
límites, generación y longitud exacta ANTES de reservar/leer el cuerpo.
Al decodificar se verifican identidad y cobertura por rank, buffers binarios,
fronteras, población, célula y mensajes; cada fila recibida debe coincidir con
la fila propia del emisor en la generación anterior. El cliente contrasta además
P, dimensiones, selección y generación con su petición.

Se conserva el plazo absoluto de operación del cliente y el de 30 s del worker;
las colectivas dependen de la supervisión externa ante un rank bloqueado.
Versión 2 y anteriores se rechazan; cliente y worker deben recompilarse juntos.
El formato CSV v2 y el alcance temporal experimental permanecen iguales.

## Aislamiento y validación

El puntero de captura de step_mpi es null por defecto. ADVANCE y RUN_BATCH usan
ese camino: sin snapshots didácticos, probes, recopilación, serialización ni
colectivas didácticas. La evaluación normal de B3/S23 no construye un registro
de vecinos. El máximo entre ranks, barreras y alcance medido de RUN_BATCH siguen
igual; la instrumentación de captura no es una operación de benchmark.

`mpi_trace_engine_{1,2,4,7}` contrasta directamente buffers y direcciones reales,
halos, célula fronteriza, población, ranks vacíos, seis generaciones y equivalencia
contra evolución MPI sin captura y oráculo secuencial. Buffers, mensajes y selección de captura
inválidos en un rank se rechazan colectivamente antes de mutar el estado.
La validación de capturas comprueba bordes globales cero en los TRES buffers,
también en ranks vacíos; solo los halos internos pueden conservar valores viejos.
`mpi_trace_client` comprueba lo mismo por IPC, recarga e interoperabilidad con
ADVANCE/RUN_BATCH, límites, corrupción de codec y respuestas inválidas; inyecta
plazos y caídas para verificar diagnóstico y limpieza confirmada.
`mpi_client` comprueba además solicitudes TRACE_STEP inválidas contra el worker
real, ausencia de mutación, cabeceras/versiones incompatibles y cierre coordinado.
No se exige aceleración ni una duración mínima.

La revisión añade prueba real de ADVANCE/RUN_BATCH sobre 129×129 células
(supera el límite didáctico), conservación de tablero/población/generación tras
rechazo remoto y compatibilidad de versiones en handshake y peticiones.
`mpi_trace_failure_worker` se compila solo con BUILD_TESTING y una definición
exclusiva de prueba: falla después del paso MPI real. Se comprueba generación 1,
diagnóstico, ausencia de reintento/reinicio implícito y limpieza confirmada. El
worker habitual no contiene esa inyección activa ni una opción de ejecución
para habilitarla. La etapa posterior incorpora el controlador descrito en DIDACTIC_CONTROLLER.md;
las vistas MPI de FTXUI muestran buffers, mensajes y evaluación capturados.

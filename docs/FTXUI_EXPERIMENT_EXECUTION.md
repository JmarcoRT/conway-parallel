# Ejecución experimental desde FTXUI (Fase 7D)

Los formularios entregan configuración validada al controlador `ExperimentExecution`.
La solicitud congela configuración, identificación y dimensiones. Un patrón se
copia una vez antes de publicar el trabajo; un tablero generado se construye una
sola vez en el worker. No se escriben archivos de entrada.

El worker persistente prepara la entrada, captura `ExperimentContext` en su
propio hilo y llama inmediatamente a `runExperiment` o `runScalingStudy`.
Los intervalos de medición pertenecen exclusivamente a esas APIs existentes:
ni generación, captura de contexto ni publicación forman parte del cronómetro.
El contexto describe el estado observado antes de entrar al ejecutor; sus ICVs
OpenMP no deben interpretarse como el tamaño efectivo del equipo de las muestras.

El controlador publica estados Listo, Preparando, Ejecutando, Finalizado y Error
bajo mutex. La UI consulta snapshots ligeros y resultados inmutables compartidos.
Cada solicitud lleva un ID; actualizaciones de otro ID o de un trabajo terminal
se descartan. El worker solo notifica mediante `PostEvent`; nunca modifica los
componentes. La notificación no transporta resultados que puedan aplicarse tarde:
la UI siempre lee el estado vigente. Una excepción conserva el resultado anterior.
Un resultado no equivalente sigue siendo un resultado finalizado, con sus flags
reales de equivalencia/validez; no se convierte en una excepción.

Esc permite regresar y editar el borrador sin cambiar la solicitud activa. Solo
hay un trabajo experimental activo. Simulación, modo didáctico y catálogo (que
calcula previsualizaciones) quedan temporalmente bloqueados para evitar trabajo
concurrente que contamine la medición. Una preparación didáctica todavía activa
impide lanzar el experimento. Las sesiones pausadas conservan sus datos.

El preflight aplica el presupuesto de payload existente de 256 MiB considerando
entrada de UI, snapshot/entrada del worker, Games de referencia y ejecución,
sesiones/previsualizaciones retenidas, muestras, workspace y resultado anterior.
Es una estimación de payload, no una garantía de RSS: excluye overhead del
asignador, FTXUI, runtime OpenMP y pilas. Los contratos privados de tamaño de los
ejecutores y este preflight deben mantenerse sincronizados si cambian las APIs.

Navegar, consultar resultados o lanzar un trabajo no hace join. Solicitar salida
con trabajo activo mantiene el bucle FTXUI y anuncia la salida pendiente; al
recibir el estado terminal se cierra. El controlador une su hilo durante la
destrucción, también ante una salida excepcional. No hay detach ni cancelación
forzada. La duración del cierre depende de la ejecución que esté pendiente.

Resultados de la sesión ofrece el trabajo actual y el último resultado recibido,
con desplazamiento mediante flechas/Home/End. No constituye un historial completo.
Comparación y escalabilidad tienen Resumen/Muestras/Método; escalabilidad incluye
un gráfico de speedup y comparación barras de tiempos. No hay exportación desde
FTXUI: configuración, resultado y contexto permanecen juntos para esa integración.

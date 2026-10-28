# Catálogo, reproducción y confirmación

El menú principal FTXUI agrupa Didáctico y Benchmark, Catálogo de patrones,
Motor y Recursos, y Salir. Los títulos de grupo no reciben foco. Flechas/Tab
recorren solo las seis acciones. Atajos del menú: D, B, C, M, R y Q/0.
No existe el antiguo acceso independiente a Visualización (tampoco el atajo 2).
El encabezado conserva el patrón **confirmado** y el motor configurado.

Recursos abre hilos para OpenMP y procesos para MPI. Secuencial informa un hilo
de cálculo y no permite modificar recursos; los ajustes de otros motores se
conservan. Seleccionar MPI no crea procesos. No se habilita sobresuscripción.

## Candidato frente a entrada confirmada

Resaltar un archivo carga un candidato validado. Su entrada inicial pertenece a
`CatalogPreview`, separada de `InteractiveState::input`. La ficha reproduce una
`SimulationSession` fijada a Secuencial y no hereda el motor de la configuración.
La entrada inicial nunca se obtiene del tablero evolucionado ni se relee al
confirmar: U / Usar patrón copia exclusivamente la entrada validada conservada.
Explorar, reproducir y cancelar no modifica Didáctico ni el patrón cargado para
Benchmark. Las entradas generadas del benchmark conservan su configuración.

Cada candidato empieza en generación 0 y pausado. Espacio/P reproduce/pausa;
R reinicia y pausa. N permite un paso manual en la ficha pausada. Enter en la
lista abre la ficha; U confirma y vuelve al destino. Enter en la ficha es también
la acción de confirmar. M abre ruta manual; Enter valida y abre un candidato
externo sin confirmar. Una ruta inválida conserva candidato y entrada confirmada;
Esc cancela la ruta. El archivo externo no se copia al catálogo de disco.

En terminal amplia hay lista y ficha con descripción, cuadrícula y reproducción.
En pequeña se alterna lista/ficha con D/Tab; Esc desde la ficha vuelve a la lista,
y desde la lista abandona. X abandona directamente. Flechas en ficha desplazan
el tablero y PgUp/PgDn los metadatos. Generación, reproducción y ayuda se conservan
visibles; a 40×18 se reduce el detalle para reservar al menos una fila del tablero.
Sin color se conservan los bloques de estado y el foco inverso. Redimensionar no
reinicia ni confirma.

## Ejecución y cierre

`SimulationSession` mantiene por defecto su comportamiento local síncrono y MPI
asíncrono para los consumidores existentes. La opción explícita `backgroundLocal`
se restringe a Secuencial y reutiliza su worker, sin otro ejecutor de simulación.
No calcula Conway en Render ni construye NEXT especulativo para el catálogo.
Solo una petición está en curso por sesión. Reinicio/cierre invalidan entregas
anteriores mediante epoch; el worker termina la operación actual y ejecuta el
control pendiente. Cambiar candidato retira la sesión anterior; sus entregas se
recogen en ella y no pueden sobrescribir el candidato nuevo.

Salir pausa, cancela el temporizador y solicita cierre. Las sesiones retiradas
se retienen hasta closed(), evitando unir en la UI un hilo que aún calcula.
`launchExperiment` y el benchmark histórico rechazan mediciones mientras el
catálogo no esté drenado. Las protecciones de propiedad MPI didáctica permanecen.
El cierre de aplicación espera catálogo, didáctico y experimentos activos.

La lectura/validación de archivos y la construcción/copia inicial de buffers
siguen siendo síncronas; un archivo muy grande puede demorar su selección. La
reproducción secuencial se ejecuta fuera de la UI. No hay presupuesto de tamaño
nuevo que reduzca la capacidad de los motores.

## Interfaz textual y pruebas

Sin TTY se usa el mismo grupo de acciones. D informa que requiere FTXUI; no hay
otro controlador didáctico. C abre catálogo; M carga una ruta candidata; N/P/R
operan la vista previa y U confirma el estado inicial. X vuelve sin cambiar y Q
sale del menú. EOF no confirma. P o una línea vacía alterna reproducción.

`app_runtime` valida estado inicial, entrada inmutable, reinicio/cierre pendiente
y descarte al cambiar de candidato. `catalog_preview_pty` verifica grupos, foco,
reproducción real del blinker, confirmación/cancelación, ruta inválida, recursos,
MPI no lanzado, tamaños 120×40/50×24/40×18 y modo sin color. El recorrido textual
cubre las mismas garantías de confirmación. Las pruebas de sesiones MPI, cliente,
evolución y didáctica mantienen su cobertura aunque se retiran los recorridos
antiguos de la página de visualización normal.

# Interfaz de terminal

## Arquitectura

La interfaz interactiva usa FTXUI v5.0.0, fijado en CMake al commit
`cdf28903a7781f97ba94d30b79c3a4b0c97ccce7`. CMake obtiene la dependencia con
`FetchContent`; no se requiere instalar una biblioteca FTXUI del sistema.

- `src/app/app.cpp` conserva la ruta existente para stdin no interactivo y
  selecciona la TUI cuando stdin y stdout están conectados a una TTY.
- `src/app/ftxui_app.cpp` conecta navegación, catálogo, selección de motor,
  configuración OpenMP, simulación y benchmark con componentes FTXUI.
- `src/app/simulation.cpp` conserva el estado de avance, reproducción y
  reinicio; `src/app/benchmark.cpp` mantiene la metodología de medición.
- `src/app/pattern_catalog.cpp` descubre archivos `.txt` en `patterns/` y
  utiliza el cargador existente para validarlos.
- `src/ui/board_view.cpp` y `src/ui/simulation_layout.cpp` siguen disponibles
  para las demos y superficies de UI no interactivas; la TUI renderiza los
  estados reales de `conway::Game` mediante elementos FTXUI.

La interfaz no duplica reglas de Conway ni modifica los motores. CURRENT muestra
la generación confirmada. NEXT local se calcula sobre una copia al actualizar
la sesión, fuera del render; MPI muestra solo CURRENT confirmado.

## Refresco y ciclo de eventos

FTXUI gestiona el bucle de eventos, los menús, los campos de entrada y la
restauración de la pantalla alternativa. En FTXUI v5.0.0, `ScreenInteractive`
serializa la pantalla completa en cada redibujado. Para no enviar ese frame
completo en cada tecla, `DifferentialOutputBuffer` rasteriza el documento
FTXUI, compara sus filas con el frame anterior y escribe solo las filas que
cambiaron usando movimientos de cursor ANSI estándar. Al cambiar las
dimensiones, se hace un repintado completo para limpiar residuos.

La reproducción usa un temporizador que publica un evento FTXUI cada 150 ms;
no mantiene un bucle de render activo ni bloquea la entrada. En pausa no se
programan eventos de reproducción, por lo que el estado visual queda quieto
hasta que el usuario actúe o el terminal cambie de tamaño. El benchmark corre
en un worker y publica su resultado al hilo de UI mediante un evento; trabaja
con una copia de la entrada activa.

La pantalla alternativa y el modo de terminal se restauran al terminar el
bucle, incluidos errores manejables. La interfaz usa secuencias ANSI estándar
para cursor y color; no utiliza salida sincronizada experimental. `NO_COLOR=1`
desactiva el color, pero conserva los bloques monocromáticos de las células.

## Navegación y controles

En el menú principal:

- Flechas y Enter: navegar y seleccionar.
- `C`: abrir el catálogo de patrones.
- `1` / `2` / `3` / `4`: benchmark, visualización, motor o configuración de
  hilos.
- `0` o `Q`: salir.

En el catálogo se muestran dinámicamente los archivos `.txt` de `patterns/`.
Los submenús de catálogo, motor e hilos comparten el mismo selector FTXUI:
flechas arriba/abajo cambian la opción resaltada, Enter confirma y Esc regresa.
`M` permite introducir una ruta manual desde el catálogo. La selección de
motor y de patrón se conserva entre pantallas cuando corresponde.

En la simulación:

- `N`: avanzar una generación.
- Espacio o `P`: pausar/reanudar.
- `R`: reiniciar desde el patrón original.
- `Esc` o `X`: pausar y volver al menú conservando la sesión; Visualización la reanuda.
- `Q`: abandonar y cerrar la sesión; una nueva visualización comienza en el patrón inicial.
- `U`: recuperar MPI desde el último snapshot confirmado después de un fallo.

Los paneles CURRENT/NEXT se organizan lado a lado en terminales amplias y
con una vista alternable cuando falta espacio. Cada celda se delimita con bordes verticales y
líneas horizontales con uniones consistentes. Las células vivas y muertas usan
superficies diferenciables; en modo monocromático se conservan bloques
distintos. Si el tablero supera el viewport, la interfaz indica las filas y
columnas visibles. La presentación informa generación, células vivas, motor y
estado. En OpenMP muestra los hilos solicitados y el tamaño de equipo observado
en una evolución. La disponibilidad reportada corresponde a procesadores
lógicos, no a núcleos físicos.

## Didáctica y experimentos

El modo didáctico prepara una generación real en un worker: StepTrace local o
TRACE_STEP MPI.
Muestra CURRENT/NEXT y trabajo por hilo con reloj didáctico independiente
(inicialmente 2 segundos por etapa a 1×). Arranca automáticamente y continúa entre
generaciones. `N` avanza una etapa en pausa; espacio pausa/reanuda incluso durante
captura; `R` reinicia conservando pausa y velocidad; `+`/`-` ajustan velocidad.
Esc solicita cierre asíncrono y vuelve al menú. No es una cronología real entre hilos; ver
[STEP_TRACE.md](STEP_TRACE.md).

Benchmark abre Comparación de motores, Escalabilidad y Resultados de la sesión.
Comparación permite elegir Secuencial, OpenMP y MPI (mínimo dos). Solo Secuencial
está marcado inicialmente. Los hilos OpenMP y procesos MPI se configuran por
separado; sin Secuencial, speedup y eficiencia respecto de él son N/D.
Escalabilidad permite elegir OpenMP/MPI y editar una lista de hilos/procesos.
Ambos formularios permiten patrón cargado o tablero generado, generaciones,
calentamientos y repeticiones. Tab/Mayús+Tab cambia campo; Espacio marca motores;
Enter activa. La entrada generada no reemplaza el patrón de simulación.

La ejecución congela configuración, genera/copia entrada antes de medir y usa
un worker persistente. Esc regresa sin cancelar el trabajo ni modificarlo al
editar. Solo hay un experimento activo; se bloquean otros cálculos competidores.
Al salir con trabajo activo se anuncia cierre pendiente hasta terminar, sin
cancelación forzada. Los errores conservan el último resultado o la evidencia
parcial de la ejecución fallida. MPI usa procesos nuevos y calentamientos por
sesión en cada turno, con cierre confirmado antes del siguiente participante.

Comparación y Escalabilidad presentan Resumen/Muestras/Método: ←→ cambia vista,
↑↓ desplaza, Home/End extremos, Esc configuración o Benchmark según procedencia.
El gráfico de escalabilidad ordena las solicitudes; cada resultado identifica
su recurso y las muestras conservan el orden experimental. El gráfico muestra speedups válidos, referencia 1.00× y eje X
categórico explícito, con escala lineal o log1p(S). No dibuja línea ideal.

En 120×40 tabla/gráfico pueden convivir si caben; en 80×24 se prioriza acceso por
desplazamiento. Valores extensos usan registros etiquetados. El redondeo es solo
visual; N/D no significa cero. Equipos efectivos son observados y pueden diferir
de solicitudes. El catálogo conserva una limitación de espacio en 50×24.

El benchmark histórico `runBenchmark()` sigue disponible para compatibilidad y
consola; su orden y resumen de equipos difieren del ejecutor configurable. Ver
[BENCHMARK_PROTOCOL.md](BENCHMARK_PROTOCOL.md). CSV existe como API, sin acción
FTXUI. Resultados de sesión no es un historial persistente.

## Dependencia y compilación

Requisitos: C++20, CMake 3.20 o posterior, OpenMP, MPI C++ y Git para que
`FetchContent` obtenga el commit fijado de FTXUI en la primera configuración.

```bash
cmake -S . -B build
cmake --build build
./build/conway
```

Para ejecutar la suite automatizada:

```bash
ctest --test-dir build --output-on-failure
bash tests/run_input_tests.sh
bash tests/test_engine_selection.sh build/conway
bash tests/test_app_functional.sh build/conway
```

Las pruebas funcionales usan una PTY cuando está disponible para comprobar
renderizado monocromático y la grilla, navegación por submenús con flechas y
Enter, cambio de patrón, avance, pausa sin escrituras periódicas,
redimensionamiento y restauración del terminal.

## Adaptación al tamaño de terminal

La composición amplia se conserva desde 120 columnas y 40 filas. Por debajo,
el encabezado usa el título de una fila; el subtítulo se conserva cuando hay
al menos 64 columnas y más de 28 filas. La barra contextual sigue mostrando
el patrón, motor e hilos solicitados.

En simulación, `V` alterna CURRENT/NEXT cuando no caben juntos. Cada panel
necesita al menos 26 columnas y la cuadrícula reserva tres columnas por celda;
se mantienen dos paneles si caben horizontalmente y hay espacio para cinco
filas lógicas (o todo el patrón, si es más corto). Las flechas desplazan el
viewport del tablero. El indicador informa el intervalo visible.

En el catálogo, `D` activa el detalle o devuelve el control a la lista.
La región activa se identifica en su encabezado. En la lista, ↑/↓ seleccionan
patrones; en el detalle, las flechas desplazan el viewport compartido de
ACTUAL/SIGUIENTE sin cambiar el patrón. En compacto se muestra una región
a la vez y `V` alterna ACTUAL/SIGUIENTE. `PgUp`/`PgDn` desplazan únicamente
los metadatos, mientras el tablero y su indicador permanecen visibles.
Enter carga el patrón seleccionado en cualquiera de las vistas. La selección nativa se conserva al redimensionar.

En didáctica, las flechas desplazan el viewport compartido por CURRENT/NEXT
en todos los tamaños. `T` alterna los tableros y la lista completa de
trabajadores; ↑/↓ y `PgUp`/`PgDn` recorren esa lista sin limitarla a ocho
trabajadores. En compacto, `V` alterna los tableros. Cuando ambas generaciones
están visibles, `V` no cambia ninguna selección interna. Los controles de
reproducción mantienen su funcionamiento. Los formularios experimentales conservan el desplazamiento
al mover el foco con Tab/Mayús+Tab; los resultados conservan sus pestañas y
el desplazamiento con flechas, Inicio y Fin.

El redimensionamiento conserva simulaciones, borradores y resultados; no
relanza experimentos. Por debajo de 40×18 se muestra un aviso de tamaño
insuficiente. Este mínimo reserva espacio para la identidad, un panel con sus
controles y la ayuda inferior. Los tableros grandes usan un viewport: para
ver otras células hay que desplazarlo. La vista amplia conserva su presentación
original, incluida la tabla resumida de trabajadores.

La geometría de Catálogo y Didáctica se calcula a partir del rectángulo que
FTXUI asigna finalmente al área de tableros, después del contenido circundante.
Se reserva espacio para cuadrículas completas y un indicador compartido con
los intervalos de filas y columnas visibles. Si ni siquiera cabe una celda
con sus controles, esa región muestra un aviso de espacio insuficiente.
Al redimensionar se conservan región activa y offsets que sigan siendo válidos;
los demás offsets se ajustan al nuevo rango sin reiniciar el estado.

## Simulación MPI local

En Seleccionar motor, M elige MPI y C abre Procesos MPI (P en el selector textual).
No se crea ningún rank hasta entrar en Visualización. El valor inicial es P=1;
se muestra una recomendación basada en afinidad/filas y se aceptan 1..1024.
No se habilita sobresuscripción. El trabajador se busca junto al ejecutable
`conway`; CMake configura la ruta compatible de `mpirun` y compila ambos targets.

Cambiar motor o P conserva el tablero, generación y referencia inicial de R.
El cambio de P se aplica al reanudar y recrea el trabajo MPI. Durante una operación
pendiente no se acumulan pasos. Pausar deja terminar el avance ya solicitado.
Los fallos conservan el último snapshot y permiten U, cambiar a un motor local,
Esc o Q. Los errores de tamaño/lanzamiento se muestran en la simulación.

La entrada didáctica MPI está habilitada: V alterna Global/Local/Explicación,
[ / ] seleccionan rank (incluidos los vacíos) y ↑↓/Home/End desplazan el contenido.
En terminal amplia muestra tablero global y cuadrícula privada del rank
seleccionado, con halos y NEXT revelados según la etapa. Véase [DIDACTIC_CONTROLLER.md](DIDACTIC_CONTROLLER.md).
Benchmark configurable admite MPI;
debe abandonarse una sesión MPI de visualización antes de medir. En resultados,
el principal MPI es el máximo entre ranks; los tiempos de arranque, carga,
solicitud y snapshot se presentan separados. Un estudio incompleto muestra su
diagnóstico, limpieza y evidencia parcial. No se añade exportación desde la UI.
Véanse [MPI_SESSION.md](MPI_SESSION.md) y [MPI_EXPERIMENTS.md](MPI_EXPERIMENTS.md).

Los resultados mantienen las pestañas y la ayuda visibles al desplazar con
↑/↓ o Inicio/Fin. La barra lateral y «Bloque actual/total» indican la posición;
los párrafos se ajustan al ancho disponible también después de redimensionar.
En formularios anchos, Entrada y Medición tienen columnas de ancho fijo para
cada tamaño de terminal, independientemente del motor seleccionado.
La barra contextual del benchmark identifica la configuración de «Simulación»;
los participantes del experimento se identifican dentro del formulario/resultado.
Los números se presentan con tres decimales (notación científica para magnitudes
pequeñas o grandes); N/D no lleva unidades ni sufijos. El formato no modifica
los datos ni limita la eficiencia al 100 %.

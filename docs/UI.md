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
la generación actual y NEXT se calcula sobre una copia del juego, sin cambiar la
simulación activa.

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
- `Esc`, `X` o `Q`: regresar al menú.

Los paneles CURRENT/NEXT se organizan lado a lado en terminales amplias y
verticalmente en las estrechas. Cada celda se delimita con bordes verticales y
líneas horizontales con uniones consistentes. Las células vivas y muertas usan
superficies diferenciables; en modo monocromático se conservan bloques
distintos. Si el tablero supera el viewport, la interfaz indica las filas y
columnas visibles. La presentación informa generación, células vivas, motor y
estado. En OpenMP muestra los hilos solicitados y el tamaño de equipo observado
en una evolución. La disponibilidad reportada corresponde a procesadores
lógicos, no a núcleos físicos.

## Didáctica y experimentos

El modo didáctico prepara una generación real en un worker con StepTrace.
Muestra CURRENT/NEXT y trabajo por hilo con reproducción relativa, pausa,
velocidad y avance. `N` siguiente, espacio reproducir, `R` reiniciar, `+`/`-`
velocidad, Esc volver. No es una cronología real entre hilos; ver
[STEP_TRACE.md](STEP_TRACE.md).

Benchmark abre Comparación de motores, Escalabilidad OpenMP y Resultados de la
sesión. Los formularios permiten patrón cargado o tablero generado, generaciones,
calentamientos, repeticiones y solicitud/lista de hilos. Tab/Mayús+Tab cambia
campo; Enter activa. La entrada generada no reemplaza el patrón de simulación.

La ejecución congela configuración, genera/copia entrada antes de medir y usa
un worker persistente. Esc regresa sin cancelar el trabajo ni modificarlo al
editar. Solo hay un experimento activo; se bloquean otros cálculos competidores.
Al salir con trabajo activo se anuncia cierre pendiente hasta terminar, sin
cancelación forzada. Los errores conservan el último resultado.

Comparación y Escalabilidad presentan Resumen/Muestras/Método: ←→ cambia vista,
↑↓ desplaza, Home/End extremos, Esc configuración o Benchmark según procedencia.
La tabla de escalabilidad se ordena por solicitud; las muestras conservan orden
experimental. El gráfico muestra speedups válidos, referencia 1.00× y eje X
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

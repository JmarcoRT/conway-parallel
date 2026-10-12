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
Flechas y Enter seleccionan; `M` permite introducir una ruta manual y `Esc`
regresa. La selección de motor y de patrón se conserva entre pantallas cuando
corresponde.

En la simulación:

- `N`: avanzar una generación.
- Espacio o `P`: pausar/reanudar.
- `R`: reiniciar desde el patrón original.
- `Esc`, `X` o `Q`: regresar al menú.

Los paneles CURRENT/NEXT se organizan lado a lado en terminales amplias y
verticalmente en las estrechas. Las células son bloques rectangulares; si el
tablero supera el viewport, la interfaz indica las filas y columnas visibles.
La presentación informa generación, células vivas, motor y estado. En OpenMP
muestra los hilos solicitados y el tamaño de equipo observado en una evolución.
La disponibilidad reportada corresponde a procesadores lógicos, no a núcleos
físicos.

## Benchmark

El benchmark muestra primero un estado de ejecución mientras trabaja fuera del
hilo de interfaz. Compara los motores Secuencial y OpenMP con la misma entrada,
generaciones y repeticiones; conserva calentamiento, medianas y verificación de
equivalencia. Los tiempos usan `std::chrono::steady_clock` y no incluyen
renderizado ni entrada/salida. La eficiencia se calcula solo para resultados
equivalentes y un conteo efectivo estable. Un tablero pequeño puede no ser
representativo por el overhead de paralelización.

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
renderizado monocromático, avance, pausa sin escrituras periódicas,
redimensionamiento y restauración del terminal.

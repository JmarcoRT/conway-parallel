# Interfaz de terminal

## Diseño

La TUI separa responsabilidades:

- `src/app/controls.cpp`: lectura de teclas, modo raw, dimensiones con
  `TIOCGWINSZ`, detección ANSI y ciclo de vida de la pantalla alternativa.
- `src/app/pattern_catalog.cpp`: descubrimiento de `.txt`, validación a través
  de `loadInput` y metadatos de patrones.
- `src/ui/layout.cpp`: menú adaptable y pantallas informativas.
- `src/ui/board_view.cpp`: representación acotada de células reales del `Game`.
- `src/ui/simulation_layout.cpp`: layout de CURRENT/NEXT y estado de ejecución.
- `src/app/app.cpp`: navegación y coordinación de los módulos.

Los renderers no leen teclado ni modifican el estado de Conway. La sesión
interactiva se dibuja sobre la pantalla alternativa y devuelve al terminal su
pantalla y configuración al salir. En una entrada no TTY se mantiene la carga
de rutas por línea para soportar pruebas y automatización.

## Patrones

En una TTY, `C` abre el catálogo descubierto desde `patterns/*.txt`. Se puede
navegar con flechas, seleccionar con Enter o usar `M` para indicar una ruta
manual. Cada entrada indica dimensiones y categoría si se reconoce. Los
archivos inválidos aparecen como tales y no sustituyen el patrón activo.
Agregar un archivo `.txt` compatible a `patterns/` lo incorpora al siguiente
escaneo del catálogo.

El cargador conserva su formato: alto y ancho en la primera línea, seguidos por
una fila de `0`/`1` por línea.

## Tablero y simulación

Las células vivas y muertas se dibujan como superficies rectangulares usando
bloques Unicode en modo monocromático o color de fondo ANSI cuando la salida es
una TTY compatible. `NO_COLOR` desactiva el color. Los paneles se disponen lado
a lado en terminales anchas y verticalmente en las estrechas. Los tableros que
no caben usan un viewport con filas/columnas omitidas indicadas.

Controles de simulación:

- Espacio: pausar/reanudar.
- `N`: avanzar una generación.
- `R`: reiniciar el patrón.
- `Esc`: regresar al menú.

La pausa no genera redibujados periódicos; el bucle espera teclas con timeout y
detecta cambios del tamaño de terminal.

## Configuración y benchmark

La configuración OpenMP de la sesión se aplica a la visualización y al
benchmark. Se muestra por separado el valor solicitado y el equipo efectivo;
los procesadores lógicos detectados no se presentan como núcleos físicos.

El benchmark conserva calentamiento, repeticiones, medianas, equivalencia y
métricas con `steady_clock`. La medición se realiza sin renderizado. La
eficiencia se informa solo con resultados equivalentes y tamaño de equipo
efectivo estable; un tablero pequeño muestra una advertencia de overhead.

## Ejecución y pruebas

Desde la raíz del repositorio:

```bash
cmake -B build -S .
cmake --build build
./build/conway
ctest --test-dir build --output-on-failure
bash tests/test_app_functional.sh
```

La suite funcional utiliza `script`/PTY cuando está disponible para comprobar
teclas, retorno al menú, redimensionamiento y restauración de terminal.

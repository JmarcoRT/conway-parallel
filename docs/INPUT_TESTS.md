# Formato de entrada, patrones y demos visuales

Base revisada: rama `develop`, commit `49eedc6`.

Este documento describe el lector y las entradas que existen en esa base. No
define un formato nuevo ni cambia las interfaces `loadInput(const std::string&)`
y `InputData`.

## Formato `.txt` actual

El archivo contiene dos dimensiones y luego las células:

1. La primera pareja de números es **alto ancho**, en ese orden.
2. Siguen exactamente `alto` tokens de fila; cada token debe tener `ancho`
   caracteres.
3. Cada carácter es `1` para una célula viva o `0` para una célula muerta.

Las dimensiones describen el tablero lógico: `alto` filas por `ancho`
columnas. El archivo no incluye el halo que usa la representación interna. Las
células se guardan por filas, de izquierda a derecha.

El lector separa tokens usando espacios en blanco. Acepta finales de línea LF o
CRLF, espacios en blanco al final y archivos sin salto de línea final. No exige
que cada token de fila ocupe una línea física separada; en los ejemplos se usa
una fila por línea para facilitar la lectura. Rechaza filas de ancho incorrecto,
caracteres distintos de `0` y `1`, y cualquier token adicional después del
tablero. No hay sintaxis para comentarios ni espacios entre células de una fila.

### Ejemplo mínimo válido

Este archivo representa un tablero de alto 2 y ancho 3, con tres células vivas:

```text
2 3
010
101
```

Al iniciar, la aplicación integrada no tiene una entrada activa. En una TTY,
`C` abre el catálogo dinámico de `patterns/*.txt`; se elige un patrón con las
flechas y Enter. `M` permite introducir una ruta manual. En entrada no TTY,
`C` solicita la ruta como una línea de texto, para conservar la compatibilidad
con scripts. También se puede entrar a Visualización sin patrón y cargarlo
cuando la aplicación lo solicite. Las rutas como `patterns/blinker.txt` se
resuelven desde el directorio actual.

## Patrones disponibles y momentos para la demo

Las dimensiones de esta tabla se muestran como **alto × ancho**, igual que en
la primera línea de cada archivo.

| Archivo | Alto × ancho | Vivas iniciales | Momento y resultado esperado |
|---|---|---|---|
| `patterns/blinker.txt` | 5 × 5 | 3 | Generaciones 0–2: vertical, horizontal y vuelve a vertical |
| `patterns/block.txt` | 4 × 4 | 4 | Generaciones 0–1: el bloque permanece estable |
| `patterns/glider.txt` | 6 × 7 | 5 | Generaciones 0–4: se desplaza una fila hacia abajo y una columna a la derecha |
| `patterns/toad.txt` | 6 × 6 | 6 | Generaciones 0–2: cambia de fase y vuelve al estado inicial |
| `patterns/beacon.txt` | 6 × 6 | 8 | Generaciones 0–2: alterna entre 8 y 6 células vivas y vuelve |
| `patterns/pulsar.txt` | 17 × 17 | 48 | Generaciones 0–3: población 48 → 56 → 72 → 48; vuelve al estado inicial |
| `patterns/gosper_glider_gun.txt` | 18 × 50 | 36 | Generación 30: emite un planeador; la prueba registra 41 células vivas |
| `patterns/test.txt` | 5 × 5 | 3 | Entrada de prueba que conserva el Blinker |

Las fixtures `single_dead.txt`, `single_alive.txt` y `rectangle.txt` de
`tests/fixtures/input/valid/` cubren dimensiones pequeñas y rectangulares.

Para una presentación breve, conviene empezar por Blinker, Block y Glider:
cambian poco entre generaciones y el resultado se reconoce con facilidad.
Pulsar y Gosper son opciones para una vista más amplia o una demostración larga.
Las pruebas existentes comprueban los estados esperados en
`tests/input_tests.cpp` y `tests/conway_correctness_tests.cpp`; no hace falta
añadir tests para reutilizar estos casos en la demo.

## Cargar y ejecutar una demo

Desde la raíz del repositorio, configura y compila la aplicación y la demo
textual:

```bash
cmake -B build -S .
cmake --build build --target conway conway_view_demo
```

La aplicación integrada inicia sin una entrada activa. Ejecuta:

```bash
./build/conway
```

En TTY, `C` abre el catálogo: elige Blinker con flechas/Enter o `M` para
una ruta manual. En pipe, `C` pide la ruta directamente; luego
elige `2` para Visualización. También puedes elegir Visualización primero y
la aplicación pedirá una ruta si aún no hay una entrada cargada. Ejecuta desde
la raíz para que las rutas `patterns/...` se resuelvan correctamente.

La pantalla muestra `CURRENT` y `NEXT` calculado desde el estado real del
motor. Dentro de la simulación, `N` avanza una generación, Espacio o `P`
inician/pausan la reproducción y `R` reinicia. En TTY, `Esc`, `X` o `Q`
vuelven al menú; en modo no TTY, `X` o `Q` lo hacen.

Para una ejecución corta que imprime tableros y progreso en texto:

```bash
./build/conway_view_demo patterns/blinker.txt 2
```

Muestra las generaciones 0, 1 y 2 junto con `NEXT` para cada una. La salida
registrada de la demo textual y un extracto de la aplicación integrada están en
[`evidencias/blinker_demo.txt`](../evidencias/blinker_demo.txt).

## Cómo reproducir

Desde la raíz del repositorio, con Bash, herramientas habituales de Linux y un
compilador C++20:

```bash
bash tests/run_input_tests.sh
```

Puede elegirse el compilador con `CXX=g++-14 bash tests/run_input_tests.sh`.
`CXX` debe identificar un ejecutable, sin argumentos adicionales.
El script compila con advertencias, ejecuta la suite C++ y comprueba el programa
real. Devuelve cero solamente si esas comprobaciones pasan. Usa un directorio
temporal propio que elimina al finalizar; no reemplaza patrones del repositorio.
Puede invocarse desde otro directorio usando la ruta al script.

Para ejecutar únicamente la suite C++:

```bash
mkdir -p build
g++ -std=c++20 -fopenmp -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Iinclude \
    tests/input_tests.cpp src/input/input.cpp src/core/game.cpp \
    src/core/engines/sequential.cpp src/core/engines/openmp.cpp src/core/bs23.cpp \
    -o build/input_tests
./build/input_tests "$PWD"
```

Cada línea `PASS` o `FAIL` identifica el caso; los errores se acompañan de un
diagnóstico. Las comprobaciones siguen activas aunque se compile con `NDEBUG`.

También se registra la suite normal en CTest:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

La configuración principal sigue requiriendo OpenMP y MPI. CTest ejecuta la
suite C++; las tres comprobaciones del ejecutable se ejecutan mediante el script.
En Fedora x86_64 se verificó también la configuración indicando el wrapper de MPI
explícitamente, sin necesitar el comando `module`:

```bash
cmake -S . -B build -DBUILD_TESTING=ON \
    -DMPI_CXX_COMPILER=/usr/lib64/openmpi/bin/mpicxx
cmake --build build
ctest --test-dir build --output-on-failure
```

Dependencias instaladas en Fedora: `cmake`, `gcc-c++`, `make`, `openmpi` y
`openmpi-devel`. La integración con CTest pasó en el entorno registrado en la evidencia.

## Casos inválidos

Los archivos siguientes se encuentran en `tests/fixtures/input/invalid/`.
Para reproducirlos, ejecutar la suite y localizar la línea con el nombre indicado.
El resultado esperado es una excepción `std::runtime_error` con un diagnóstico
que identifique la causa; la prueba falla si se acepta la entrada o se lanza otro
tipo de excepción.

| Archivo/caso | Resultado esperado |
|---|---|
| `missing_dimensions.txt` | No se pudieron leer las dimensiones |
| `nonnumeric_dimensions.txt` | No se pudieron leer las dimensiones |
| `dimensions_out_of_range.txt` | Rechazo del número que no cabe en `size_t` |
| `zero_height.txt`, `zero_width.txt` | Las dimensiones deben ser mayores que cero |
| `missing_rows.txt` | Faltan filas |
| `extra_rows.txt`, `trailing_data.txt` | Datos adicionales después del tablero |
| `short_row.txt`, `long_row.txt` | La fila no tiene el ancho esperado |
| `spaced_cells.txt` | La fila no tiene el ancho esperado |
| `invalid_character.txt`, `nonbinary_digit.txt` | Solo se permiten `0` y `1` |
| `empty_file` | No se pudieron leer las dimensiones |
| `missing_file` | No se pudo abrir el archivo; el diagnóstico incluye la ruta |

El archivo vacío se genera temporalmente. La ruta inexistente está dentro de un
directorio temporal nuevo, para garantizar que no haya un archivo previo.
Los casos `crlf`, `trailing_whitespace` y `no_final_newline` también se generan
con bytes explícitos para evitar alteraciones por el editor o la configuración de Git.

## Cargas sucesivas

| Caso | Pasos | Resultado esperado |
|---|---|---|
| `change_file_A_B_A` | Cargar Block, conservar una copia, cargar Glider, construir `Game`, volver a Block | Se sustituyen dimensiones y células; la copia original no cambia |
| `failed_change_invalid_character.txt` | Cargar Block; intentar asignar una carga inválida; cargar Glider | Se captura el error, Block sigue intacto y luego Glider carga correctamente |
| `failed_change_missing.txt` | La misma secuencia con una ruta inexistente | Error de apertura, conservación de Block y recuperación posterior |
| `glider_moves_after_4_steps` | Cargar Glider, evolucionar un juego y crear otro desde la misma entrada | La entrada original conserva su estado y puede reutilizarse |

En la aplicación integrada, un cambio de entrada fallido muestra el error y
conserva la entrada anterior. Esto se implementa en `src/app/app.cpp`; las
pruebas de asignación sucesiva también cubren la reutilización de `InputData`.

## Programa completo

El script `tests/run_input_tests.sh` compila la aplicación integrada en un
directorio temporal y comprueba la selección de entrada mediante el menú:

| Caso | Resultado esperado |
|---|---|
| `executable_loads_valid_pattern` | Se carga Block; el menú muestra la ruta activa y dimensiones 4 × 4 |
| `executable_invalid_pattern` | El menú presenta el diagnóstico por una célula no binaria |
| `executable_missing_file` | El menú presenta el diagnóstico de ruta inexistente |

Estas comprobaciones cubren la carga y los errores de entrada. La evolución
visual se reproduce aparte con los comandos descritos en la sección anterior.

## Regresiones de dimensiones corregidas

Las tres regresiones históricas se ejecutan en CTest normal mediante
`input_dimension_regressions`. También pueden aislarse:

```bash
./build/input_tests "$PWD" --dimension-regressions
ctest --test-dir build -R '^input_' --output-on-failure
```

Dimensiones negativas/no representables se rechazan antes de reservar. Se
comprueban producto, halo, capacidad y límite de índices del motor. Game exige
cantidad exacta de células y valores binarios antes de construir buffers.
El caso desbordado usa alto SIZE_MAX/2+1 y ancho 2, sin reservar un tablero grande.
La suite de entradas en memoria cubre ceros, células faltantes/sobrantes,
valores 2/255, productos/halo y límites representables.

[INPUT_TEST_EVIDENCE.md](INPUT_TEST_EVIDENCE.md) conserva el fallo original de
Semana 5 como antecedente, no como estado actual. `bad_alloc` aún puede ocurrir
para entradas representables: estas comprobaciones no garantizan RAM disponible.

## Trazabilidad

Antecedente de la documentación original (la consolidación actual revisa también
Game, validación compartida y CTest): `README.md`,
`docs/ARCHITECTURE.md`, `docs/CONTRIBUTING.md`, `src/input/input.cpp`,
`include/input/input.hpp`, `src/app/app.cpp`, `src/ui/demo_view.cpp`,
`tests/input_tests.cpp`, `tests/conway_correctness_tests.cpp` y los archivos
de `patterns/` en `develop` (`49eedc6`). No se consultaron fuentes externas
ni se agregaron bibliotecas. Las referencias de instalación Fedora que siguen
pertenecen a la documentación previa.

Referencias para la instalación de herramientas en Fedora:
[CMake en Fedora](https://developer.fedoraproject.org/tech/languages/c/cmake.html),
[el paquete openmpi-devel](https://packages.fedoraproject.org/pkgs/openmpi/openmpi-devel/)
y [los módulos de entorno de Fedora](https://fedoraproject.org/wiki/Packaging/EnvironmentModules).
La ruta explícita de `mpicxx` se verificó localmente y funcionó con CMake.

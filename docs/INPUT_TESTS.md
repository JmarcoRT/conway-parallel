# Semana 5 — Patrones y pruebas funcionales de entrada

## Alcance y formato existente

Base: `origin/develop`, commit `adcee7b`. Se usa `loadInput(const std::string&)`
y `InputData` tal como están implementados, sin modificar interfaces ni el lector.

La primera pareja de valores representa **alto ancho**, en ese orden. Después
aparecen `alto` filas de `ancho` caracteres binarios. No se incluyen halos.
El vector devuelto contiene las células por filas, de izquierda a derecha.

El lector usa extracción por espacios en blanco: admite LF, CRLF, espacios al
final y ausencia de salto final. No comprueba límites de líneas físicas; aquí
se presentan los ejemplos con una fila lógica por línea. No admite espacios
entre células de una misma fila, comentarios ni contenido adicional.

`main` carga únicamente `patterns/test.txt` relativo al directorio de ejecución.
Todavía no hay selección interactiva de archivo ni gestión de entrada activa.
Las pruebas de cambio de archivo verifican llamadas sucesivas al lector y
asignación a una variable del test; **no prueban un menú existente**.

## Ejemplos válidos

| Archivo | Alto × ancho | Vivas | Comprobación |
|---|---|---|---|
| `patterns/block.txt` | 4 × 4 | 4 | Carga exacta y estabilidad después de un paso |
| `patterns/blinker.txt` | 5 × 5 | 3 | Vertical → horizontal → vertical en dos pasos |
| `patterns/glider.txt` | 6 × 7 | 5 | Carga rectangular; desplazamiento una fila y una columna tras cuatro pasos |
| `patterns/test.txt` | 5 × 5 | 3 | Se conserva el Blinker original |
| `tests/fixtures/input/valid/single_dead.txt` | 1 × 1 | 0 | Tamaño mínimo con célula muerta |
| `tests/fixtures/input/valid/single_alive.txt` | 1 × 1 | 1 | Tamaño mínimo con célula viva |
| `tests/fixtures/input/valid/rectangle.txt` | 2 × 4 | 4 | Orden de dimensiones y células: `0101`, `1010` |

En cada carga se comprueban las dimensiones, todas las células y su orden.
Después se construye `Game` y se comprueban generación cero, estado y conteo.
Los estados esperados de los patrones están escritos explícitamente en los
tests; no se calculan mediante otra copia del algoritmo del motor.

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

Para ejecutar únicamente la suite C++ o investigar sus fallos:

```bash
mkdir -p build
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Iinclude \
    tests/input_tests.cpp src/input/input.cpp src/core/game.cpp -o build/input_tests
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

La conservación tras un fallo corresponde a `active = loadInput(path)`: si el
lado derecho lanza una excepción, no se realiza la asignación. La futura aplicación
deberá decidir cómo mostrar el error y gestionar su entrada activa.

## Programa completo

El script coloca una copia de entrada en `patterns/test.txt` dentro del directorio
temporal y ejecuta el `main` real desde allí:

| Caso | Resultado esperado |
|---|---|
| `executable_valid_block` | Salida 0; generaciones 0, 1 y 2 con el Block intacto; stderr vacío |
| `executable_invalid_file` | Salida 1; stdout vacío; `Error: El tablero solo puede contener 0 y 1.` |
| `executable_missing_file` | Salida 1; stdout vacío; `Error: No se pudo abrir el archivo: patterns/test.txt` |

La salida se compara completa. Las ejecuciones del programa son procesos
separados y no representan cambio interactivo durante una sesión.

## Regresiones pendientes de dimensiones

Se incluyen aparte tres comprobaciones que **fallan con el lector actual**:

```bash
bash tests/run_input_tests.sh --dimension-regressions
# O con el binario ya compilado:
./build/input_tests "$PWD" --dimension-regressions
```

Este modo ejecuta solamente las tres regresiones y devuelve 1 si persisten.
No convierte defectos conocidos en resultados aprobados. No forma parte del
CTest normal; debe ejecutarse explícitamente al revisar la validación de dimensiones.

| Caso | Resultado deseado | Resultado actual |
|---|---|---|
| `negative_height.txt` (`-1 1`) | Rechazo explícito de dimensiones inválidas | `std::length_error` desde `vector::reserve` |
| `negative_width.txt` (`1 -1`) | Rechazo explícito de dimensiones inválidas | `std::length_error` desde `vector::reserve` |
| `dimension_product_overflow` | Rechazo explícito del tamaño no representable | Error de filas faltantes después de desbordar el producto |

La última entrada se genera con `height = SIZE_MAX / 2 + 1`, `width = 2` y sin
filas. El producto sin signo se convierte en cero, por lo que el caso no solicita
una reserva enorme. No es un benchmark ni una prueba de agotamiento de memoria.

Los detalles y propuestas de corrección se registran en
[INPUT_TEST_EVIDENCE.md](INPUT_TEST_EVIDENCE.md). No se modifica la interfaz ni
la implementación del lector en esta entrega.

## Trazabilidad

Fuentes utilizadas: `README.md`, `docs/ARCHITECTURE.md`, `docs/CONTRIBUTING.md`,
`src/input/input.cpp`, `include/input/input.hpp`, `src/core/game.cpp`,
`src/main.cpp` y `patterns/test.txt` de la base indicada. Para preparar los patrones
y las pruebas no se consultaron fuentes externas ni se agregaron bibliotecas de pruebas.

Referencias para la instalación de herramientas en Fedora:
[CMake en Fedora](https://developer.fedoraproject.org/tech/languages/c/cmake.html),
[el paquete openmpi-devel](https://packages.fedoraproject.org/pkgs/openmpi/openmpi-devel/)
y [los módulos de entorno de Fedora](https://fedoraproject.org/wiki/Packaging/EnvironmentModules).
La ruta explícita de `mpicxx` se verificó localmente y funcionó con CMake.

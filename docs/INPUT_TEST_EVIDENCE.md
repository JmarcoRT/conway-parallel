# Evidencia — Semana 5

> Registro histórico de la revisión indicada, no guía del HEAD actual.
> Para comandos y estado vigente, consultar [el índice](README.md) y
> [pruebas actuales](TESTING.md). Las regresiones de dimensiones fueron
> corregidas en la consolidación técnica; no reinterpretar las salidas originales.


## Estado revisado

- Fecha: 25 de septiembre de 2026.
- Base: `origin/develop`, `adcee7b` (`docs: actualizar README y guía de contribución`).
- No se modificaron `src/`, `include/`, interfaces compartidas ni el patrón original.

## Entorno

- Linux x86_64, kernel `7.2.5-200.fc44.x86_64`.
- `g++ (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2)`.
- C++20; `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.
- Compilación directa sin advertencias.
- CMake 4.3.0; Make 4.4.1; Open MPI y openmpi-devel 5.0.9.
- OpenMP detectado mediante GCC (`-fopenmp`).
- Configuración, compilación limpia y CTest verificados.
- Este entorno difiere del Ubuntu 24.04 / GCC 14.2 de referencia. No se hicieron mediciones de rendimiento.

## Resultados observados

Comando ejecutado desde la raíz:

```bash
bash tests/run_input_tests.sh
```

| Grupo | Resultado |
|---|---|
| Ejemplos válidos y construcción de `Game` | 7 PASS |
| LF/CRLF, espacios finales y ausencia de salto final | 3 PASS |
| Fixtures inválidos normales | 13 PASS |
| Archivo vacío e inexistente | 2 PASS |
| Cambio A→B→A y recuperación tras dos cargas fallidas | 3 PASS |
| Block, Blinker y Glider: evolución esperada | 3 PASS |
| Ejecutable: carga válida, formato incorrecto y archivo inexistente | 3 PASS |

Extracto de la salida real:

```text
Resultado: 31 PASS, 0 FAIL
PASS executable_valid_block (exit 0, exact output, empty stderr)
PASS executable_invalid_file (exit 1, exact diagnostic, empty stdout)
PASS executable_missing_file (exit 1, exact diagnostic, empty stdout)
Resultado ejecutable: 3 PASS, 0 FAIL
```

Código de salida: **0**. Los pasos y expectativas individuales están en
[INPUT_TESTS.md](INPUT_TESTS.md).

### Compilación y CTest

```bash
cmake -S . -B build -DBUILD_TESTING=ON \
    -DMPI_CXX_COMPILER=/usr/lib64/openmpi/bin/mpicxx
cmake --build build --clean-first -j2
ctest --test-dir build --output-on-failure
```

La configuración y la compilación de `conway` e `input_tests` finalizaron con
código 0. CTest informó:

```text
1/1 Test #1: input_functional .................   Passed
100% tests passed, 0 tests failed out of 1
```

Ese único test de CTest agrupa las 31 comprobaciones C++; no representa casos
adicionales. El script funcional obtuvo 31 + 3 PASS. El ejecutable
`./build/conway` mostró el Blinker vertical, horizontal y vertical en
las generaciones 0, 1 y 2, con salida 0. Los diagnósticos de las tres regresiones
de dimensiones se detallan abajo.

## Defectos reproducidos en el lector existente

Comando ejecutado:

```bash
bash tests/run_input_tests.sh --dimension-regressions
```

Salida real:

```text
FAIL negative_height.txt: Excepcion distinta de runtime_error: vector::reserve
FAIL negative_width.txt: Excepcion distinta de runtime_error: vector::reserve
FAIL dimension_product_overflow: Diagnostico inesperado: Faltan filas en el tablero.
Resultado: 0 PASS, 3 FAIL
```

Código de salida: **1**. Son tres comprobaciones de defectos pendientes,
separadas explícitamente de la suite normal; no se consideran aprobadas.

### INPUT-01 — Dimensiones negativas llegan a la reserva de memoria

En `src/input/input.cpp`, las dimensiones se extraen directamente a `size_t`
(líneas 16–19) y solo se comparan contra cero (línea 25). En este entorno, `-1`
se convierte al máximo valor sin signo. `cells.reserve` (línea 32) lanza
`std::length_error` con el mensaje `vector::reserve`.

Los archivos se rechazan, pero por la operación de memoria en lugar de una
validación de entrada, y el diagnóstico no explica el problema al usuario.
Se propone validar el signo y el rango antes de reservar memoria, manteniendo
el contrato de `loadInput`. No se aplicó la corrección.

### INPUT-02 — El producto de dimensiones no se valida

En la misma línea 32 se calcula `width * height` sin comprobar desbordamiento.
Para `height = SIZE_MAX / 2 + 1` y `width = 2`, el resultado es cero. El lector
continúa y reporta filas faltantes, ocultando que el tamaño ya era inválido.

Se propone comprobar la multiplicación y el límite del contenedor antes de
reservar. No se aplicó la corrección; cualquier cambio de interfaz compartida
debe coordinarse antes de implementarlo.

## Límites e impacto

- No hay un menú de cambio de archivo que pueda validarse; se probaron llamadas sucesivas al lector.
- Se añadieron patrones, fixtures, un runner C++, un script Bash, documentación y el registro de CTest.
- No se agregaron dependencias de producción ni se alteró la configuración requerida de MPI/OpenMP.
- Configuración CMake, compilación limpia y CTest verificados en Fedora con la ruta explícita de MPI.

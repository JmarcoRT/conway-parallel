# Semana 4 — Casos de prueba y criterios de equivalencia

## 0. Alcance y base revisada

Base: `origin/develop`, commit `adcee7b` (`docs: actualizar README y guía de
contribución`). No se modifica ninguna interfaz existente (`InputData`,
`loadInput`, `conway::Game`); todo lo aquí descrito se apoya en el motor
secuencial tal como está implementado hoy.

**Coordinación con trabajo en curso.** Existe una rama sin fusionar,
`origin/test/patrones-entrada` (título interno: "Semana 5 — Patrones y
pruebas funcionales de entrada"), que ya agrega `patterns/block.txt`,
`patterns/blinker.txt`, `patterns/glider.txt` y una suite
(`tests/input_tests.cpp`) enfocada en el módulo `input` (parseo válido e
inválido) que de paso comprueba Block, Blinker y Glider durante algunas
generaciones. Esta entrega:

- **reutiliza los mismos tres patrones y los mismos estados esperados** que
  esa rama (se verificaron de forma independiente aquí, ver §4, y coinciden
  exactamente), para no dejar dos versiones distintas de un mismo patrón
  circulando;
- **no duplica** las pruebas de carga/parseo de `input` (formato inválido,
  CRLF, cambio de archivo, etc.), que ya están cubiertas allí;
- **agrega** lo que esa rama no cubre y que es el objeto de esta semana:
  más patrones de referencia (Toad, Beacon, Pulsar), la definición formal
  de los criterios de equivalencia para comparar motores entre sí, y una
  herramienta reutilizable para aplicarlos.

Antes de fusionar ambas ramas a `develop` conviene revisar en conjunto que
`patterns/block.txt`, `patterns/blinker.txt` y `patterns/glider.txt` no
queden duplicados con contenido distinto.

## 1. Formato del archivo de entrada (confirmado en el código)

Leído directamente de `src/input/input.cpp`, `loadInput`:

```text
<alto> <ancho>
<alto> filas de <ancho> caracteres, cada uno '0' o '1'
```

- La primera línea trae **alto y ancho, en ese orden** (no ancho×alto).
- El lector usa extracción por flujo (`>>`), por lo que tolera LF o CRLF,
  espacios/tabs al final de línea y ausencia de salto de línea final.
- No admite espacios entre celdas de una misma fila, comentarios ni datos
  adicionales después del tablero (se rechaza con error).
- `InputData::cells` guarda las celdas en orden de fila (row-major), sin
  halo: el halo de una celda lo agrega `Game` al construirse.

## 2. Condición de borde del motor actual (relevante para elegir tableros)

`conway::Game` reserva `(width+2) x (height+2)` celdas, pero **el halo nunca
se actualiza entre generaciones**: siempre vale 0 (`src/core/game.cpp`,
`step()` lee `current_` directamente, incluidas las posiciones de halo, que
solo se inicializan a 0 y jamás se modifican). Esto equivale a un borde
muerto fijo (no toroidal). Es el comportamiento correcto y estándar para un
tablero finito, pero implica una condición práctica para los casos de
prueba: **el patrón debe mantenerse, en todas las generaciones que se
evalúen, a al menos una celda de distancia del borde real del tablero**. Si
una célula naciera fuera de las filas/columnas lógicas, `Game` no tiene
dónde representarla y el resultado dejaría de coincidir con el tablero
infinito de referencia. Por eso cada patrón de la tabla siguiente se define
sobre un tablero con margen explícito, y el número de generaciones
recomendado se eligió para no tocar ese borde.

## 3. Catálogo de casos de prueba

Todos los patrones están en `patterns/` en el formato de §1. Los estados
intermedios de Toad, Beacon y Pulsar se obtuvieron ejecutando el motor
secuencial ya validado con Block/Blinker/Glider (ver §4) a partir del
estado inicial tomado de LifeWiki; se contrastaron además contra el periodo
y, en el caso del pulsar, la firma de población documentados en la fuente.

| Caso | Archivo | Tablero (alto×ancho) | Generaciones a ejecutar | Resultado esperado | Fuente |
|---|---|---|---|---|---|
| Block | `patterns/block.txt` | 4×4 | 1 (o cualquier número; es un *still life*) | Permanece idéntico, 4 células vivas siempre. | Patrón estándar; LifeWiki [Block](https://conwaylife.com/wiki/Block) |
| Blinker | `patterns/blinker.txt` | 5×5 | 2 | Vertical → horizontal (gen. 1) → vertical original (gen. 2). 3 células vivas en todo momento. | LifeWiki [Blinker](https://conwaylife.com/wiki/Blinker) |
| Toad | `patterns/toad.txt` | 6×6 | 2 | Fase A (gen. 0) → fase B (gen. 1) → fase A (gen. 2). 6 células vivas en ambas fases. | LifeWiki [Toad](https://conwaylife.com/wiki/Toad) (RLE `b2o$o$3bo$b2o!`) |
| Beacon | `patterns/beacon.txt` | 6×6 | 2 | Fase densa, 8 vivas (gen. 0) → fase dispersa, 6 vivas (gen. 1) → fase densa original (gen. 2). | LifeWiki [Beacon](https://conwaylife.com/wiki/Beacon) (dos bloques 2×2 en contacto diagonal) |
| Glider | `patterns/glider.txt` | 6×7 | 4 (ver §2: con este tablero no deben ejecutarse muchas más sin ampliarlo) | Misma forma, desplazada (+1 fila, +1 columna). 5 células vivas en todo momento. | LifeWiki [Glider](https://conwaylife.com/wiki/Glider) |
| Pulsar | `patterns/pulsar.txt` | 17×17 (patrón 13×13 con margen 2) | 3 | Vuelve exactamente al estado inicial. Población: 48 (gen. 0) → 56 (gen. 1) → 72 (gen. 2) → 48 (gen. 3). | LifeWiki [Pulsar](https://conwaylife.com/wiki/Pulsar), RLE oficial; conocido como *CP 48-56-72* |

Los estados exactos, celda por celda, de cada generación están escritos en
`tests/conway_correctness_tests.cpp` (constantes `block`, `blinkerVertical`,
`blinkerHorizontal`, `toadA`, `toadB`, `beaconDense`, `beaconSparse`,
`gliderStart`, `gliderAfter4`), y las verifica automáticamente esa suite.

## 4. Criterios de equivalencia

### 4.1 Definición (obligatoria, según el encargo)

Dos ejecuciones se consideran **equivalentes** cuando:

1. parten del mismo tablero inicial;
2. ejecutan el mismo número de generaciones;
3. terminan con el mismo tamaño de tablero;
4. cada celda tiene el mismo estado;
5. el número de células vivas coincide.

Nótese que 3 y 5 son consecuencia necesaria de 1, 2 y 4 si el motor es
determinista y respeta las reglas de Conway, pero se comprueban igual de
forma explícita: si fallan sin que 4 haya fallado, es señal de un error en
el conteo (`aliveCount()`) o en el manejo de dimensiones, no en las reglas.

### 4.2 Condiciones adicionales que deben cumplir OpenMP, Pthreads y MPI

Estas no estaban en el encargo original pero son necesarias para que el
criterio anterior tenga sentido al paralelizar:

- **Determinismo frente al número de hilos/procesos.** El resultado no debe
  depender de cuántos hilos OpenMP/Pthreads se usen ni de cuántos procesos
  MPI participen, ni del orden de llegada de mensajes. Concretamente: correr
  el mismo caso con 1, 2 y N hilos/procesos debe dar el mismo tablero final.
- **Mismas condiciones de borde.** Todas las implementaciones deben usar el
  borde muerto fijo descrito en §2 (ninguna debe envolver el tablero como
  toroide), para no invalidar la comparación por una razón ajena a la
  paralelización.
- **La validación no debe medirse.** Tal como indica `ARCHITECTURE.md` §9,
  la comparación entre motores debe hacerse fuera de la región temporizada
  del benchmark.

### 4.3 Herramienta para aplicar el criterio (lista para usarse hoy y a futuro)

Como todavía no existen los motores OpenMP/Pthreads/MPI, se preparó un
mecanismo en dos partes que ya funciona con el motor secuencial y no
requiere cambios cuando existan los demás:

- **`tests/dump_board.cpp`**: cliente mínimo que carga un archivo de
  entrada, ejecuta N generaciones con el motor (usa `Game`/`loadInput` tal
  cual, sin duplicar el algoritmo) y escribe el resultado en un formato de
  texto estable:

  ```text
  <ancho> <alto> <generación> <vivas>
  <alto> filas de <ancho> caracteres '0'/'1'
  ```

  Cuando existan binarios OpenMP/Pthreads/MPI, basta con que cada uno sea
  capaz de producir un volcado con este mismo formato (puede ser el mismo
  `dump_board.cpp` enlazado contra el motor correspondiente, o una salida
  equivalente) para poder compararlos sin escribir nada nuevo.

- **`tests/compare_boards.py`**: recibe dos volcados (referencia y
  candidata) y aplica exactamente los 5 criterios de §4.1, más una
  verificación opcional de que ambos estén en la generación esperada
  (`--require-generation`). Sale con código 0 si son equivalentes, 1 si no
  (indicando el motivo: tamaño, generación, celdas puntuales o conteo), y 2
  ante un formato de archivo inválido.

Ejemplo de uso (hoy, secuencial contra sí mismo; mañana, secuencial contra
OpenMP/Pthreads/MPI sin cambiar el comando):

```bash
./dump_board patterns/pulsar.txt 3 > secuencial.txt
./dump_board_openmp patterns/pulsar.txt 3 > openmp.txt   # cuando exista
python3 tests/compare_boards.py --require-generation 3 secuencial.txt openmp.txt
```

## 5. Cómo reproducir

Desde la raíz del repositorio, con Bash y un compilador C++20 (no requiere
CMake, OpenMP ni MPI: los tests de esta entrega compilan `core` e `input`
directamente, igual que `tests/run_input_tests.sh`):

```bash
bash tests/run_conway_correctness_tests.sh
```

Puede elegirse el compilador con `CXX=g++-14 bash tests/run_conway_correctness_tests.sh`.

Para investigar un fallo puntual o compilar a mano:

```bash
mkdir -p build
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Iinclude \
    tests/conway_correctness_tests.cpp src/input/input.cpp src/core/game.cpp \
    -o build/conway_correctness_tests
./build/conway_correctness_tests "$PWD"
```

Para generar y comparar volcados manualmente (por ejemplo, para inspeccionar
un caso o preparar la comparación contra un futuro motor paralelo):

```bash
g++ -std=c++20 -Wall -Wextra -Iinclude \
    tests/dump_board.cpp src/input/input.cpp src/core/game.cpp -o build/dump_board
./build/dump_board patterns/pulsar.txt 3 > /tmp/a.txt
./build/dump_board patterns/pulsar.txt 3 > /tmp/b.txt
python3 tests/compare_boards.py /tmp/a.txt /tmp/b.txt
```

La evidencia de una ejecución real de todo lo anterior está en
[`TESTING_EVIDENCE.md`](TESTING_EVIDENCE.md).

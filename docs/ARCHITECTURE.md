# Arquitectura del proyecto

## 1. Propósito

Este documento define la arquitectura base de `conway-parallel`.

El objetivo es permitir que distintos integrantes desarrollen módulos en paralelo sin crear implementaciones incompatibles.

Las decisiones aquí definidas deben respetarse durante el desarrollo. Cualquier cambio que afecte interfaces comunes o la arquitectura debe coordinarse antes de implementarse.

---

## 2. Objetivo técnico

Implementar el Juego de la Vida de Conway en C++ mediante cuatro modos de ejecución:

- Secuencial
- OpenMP
- Pthreads
- MPI

Todos los modos deben producir el mismo estado del tablero para una misma configuración inicial.

La implementación secuencial será utilizada como referencia de corrección para validar las implementaciones paralelas.

El proyecto tendrá dos formas principales de uso:

- `benchmark`: comparar rendimiento entre implementaciones;
- `visualización`: observar cómo cada implementación construye las generaciones.

La aplicación integra actualmente los modos Secuencial y OpenMP. Pthreads y MPI
siguen siendo implementaciones independientes, no seleccionables desde la UI.

---

## 3. Arquitectura general

```text
                    main / app
                        |
                 entrada activa
                        |
             +----------+----------+
             |                     |
         benchmark            visualización
             |                     |
             +----------+----------+
                        |
                       core
                        |
      +-------------+-------------+-------------+
      |             |             |             |
 sequential      openmp        pthreads         mpi
```

Los cuatro motores de ejecución trabajan sobre el mismo problema lógico y deben respetar las mismas reglas de Conway.

La entrada activa se mantiene mientras el programa permanezca abierto y puede reutilizarse entre `benchmark` y `visualización`.

---

## 4. Módulos

### input

Responsable de leer y validar la entrada del usuario.

Debe:

- leer archivos `.txt`;
- obtener dimensiones del tablero;
- obtener el estado inicial;
- validar formato y contenido;
- producir una representación común `InputData`.

No debe:

- aplicar reglas de Conway;
- agregar halos;
- ejecutar motores;
- medir rendimiento;
- renderizar.

La entrada representa únicamente el tablero lógico del usuario.

### core

Responsable de la representación interna del Juego de la Vida.

La representación común vive en `core::GameState` (header `include/core/game_state.hpp`), un POD con:

- ancho lógico (`width`);
- alto lógico (`height`);
- `stride = width + 2`;
- buffer `current` y buffer `next`, cada uno de tamaño `stride * (height + 2)`;
- generación actual (`generation`);
- contador de células vivas (`aliveCount`).

El tablero utiliza memoria contigua y un halo de una celda alrededor del área lógica. Para un tablero lógico:

```text
width x height
```

la representación interna será:

```text
(width + 2) x (height + 2)
```

Sobre `GameState` opera la fachada pública `conway::Game` (header `include/core/game.hpp`), que mantiene exactamente el contrato estable del proyecto: constructor desde `InputData`, `isAlive`, `setCell`, `clear`, `step`, `width`, `height`, `generation`, `aliveCount`.

La regla B3/S23 vive en `core::bs23::next_cell_state(...)` (header
`include/core/bs23.hpp`), un helper puro por celda que no incluye, importa ni
menciona nada relativo a paralelismo. `Game::step()` delega en el motor activo;
hoy están disponibles `step_sequential` y `step_openmp`. Las directivas OpenMP
permanecen en el módulo de motor, no en la regla ni en la interfaz de `Game`.

No debe contener además:

- interfaz de terminal;
- lectura de teclado;
- mediciones de rendimiento.

### Motores de ejecución: `sequential`, `openmp`, `pthreads`, `mpi`

Cada motor es un archivo bajo `core::engines::` que implementa cómo se calcula la siguiente generación sobre un `GameState`:

- Realiza **su propio recorrido** sobre las celdas lógicas (filas 1..height, columnas 1..width).
- Para cada celda, llama a la regla compartida `core::bs23::next_cell_state(current, stride, row, col)`.
- Mantiene **su propia estrategia de `aliveCount`**: `step_sequential` usa un contador local; `step_openmp` usará `reduction(+:alive)`; `step_pthreads` sumará parciales por hilo; `step_mpi` operará sobre `alive` local por rank (con `MPI_Reduce` si se requiere un total global).
- Al terminar: escribe `state.aliveCount`, llama `swap_buffers(state)` (helper trivial que sólo intercambia `current` y `next`, sin tocar `generation` ni `aliveCount`) e incrementa `state.generation`.

#### sequential

Implementación de referencia para validar OpenMP, Pthreads y MPI (hoy vive en `src/core/engines/sequential.cpp` como `core::engines::step_sequential(GameState&)`). Recorre las celdas reales secuencialmente calculando `next` a partir de `current` mediante `next_cell_state`.

#### openmp

Implementación paralela con memoria compartida mediante OpenMP en
`src/core/engines/openmp.cpp`. Opera sobre la misma `GameState` que
`sequential`, reparte el recorrido entre hilos y consolida `aliveCount` antes
de intercambiar los buffers.

#### pthreads

(Futuro.) Implementación mediante POSIX Threads. Repartirá el rango `[1..height]` entre hilos con `pthread_create`; cada hilo iterará sobre sus filas y columnas escribiendo en `state.next` con `next_cell_state`. Tras `pthread_join`, el hilo principal consolidará los `aliveCount` parciales, aplicará `swap_buffers` e incrementará `generation`.

#### mpi

(Futuro.) Implementación distribuida. Cada rank construirá su propio `GameState` local con `height = alto_local`, buffers dimensionados `stride * (alto_local + 2)` (1 fila halo arriba y 1 abajo). Su `step_*` deberá:

1. Intercambiar las filas halo con los procesos vecinos vía `MPI_Sendrecv` (o equivalente).
2. Recorrer sólo `[1..alto_local]` con `next_cell_state`.
3. Aplicar `swap_buffers` y actualizar metadatos localmente.
4. Si se requiere `aliveCount` global, consolidar con `MPI_Reduce` sobre el comm.

La forma del `GameState` y de `next_cell_state` es la misma que para `sequential`/`openmp`/`pthreads`; sólo cambia la orquestación (intercambio de halo antes del recorrido) y el ámbito del recorrido (local vs total).

### patterns

Contendrá entradas conocidas para pruebas y demostraciones, por ejemplo:

- Block;
- Blinker;
- Glider;
- Toad;
- Beacon;
- Pulsar;
- Gosper Glider Gun.

### app y UI

`src/app/` conserva la entrada y el motor seleccionados y coordina los módulos.
`SimulationSession` administra el estado de reproducción, avance y reinicio sin
añadir reglas al core. La aplicación usa `ui::board_view` para generar CURRENT
y NEXT desde copias/lecturas del `Game` real, y `ui::simulation_layout` para
presentar estado, generación, células vivas, motor y controles. La lectura de
teclado temporizada permite avanzar la reproducción sin bloquear los controles.
La interfaz consulta las dimensiones reales de la TTY con `ioctl(TIOCGWINSZ)`.
Los layouts de menú y simulación centran el contenido y reservan zonas de
encabezado, contenido y controles, con una variante compacta para terminales
pequeñas. La sesión interactiva utiliza la pantalla alternativa; los controles
restauran el modo de terminal y la pantalla al salir. La navegación y la
simulación detectan redimensionamientos mediante sondeo temporizado.

`app::discoverPatterns` enumera los archivos `.txt` de `patterns/`, valida sus
metadatos a través del cargador existente y muestra dimensiones/categorías. Los
archivos inválidos permanecen visibles con el error asociado; una carga fallida
no reemplaza la entrada actual. En modo no TTY se conserva el ingreso de rutas
por línea para compatibilidad con pruebas y automatización.

`ui::board_view` representa cada estado como bloques de celdas UTF-8 o fondos
ANSI, con fallback monocromático y viewport explícito para tableros grandes.
`ui::simulation_layout` alinea CURRENT y NEXT, centra el tablero en los paneles
y distribuye la pantalla según sus dimensiones.

Mientras está pausada, la simulación se redibuja únicamente tras una acción o
un cambio de dimensiones; el sondeo temporizado no vuelve a emitir el mismo
cuadro.

La configuración de hilos OpenMP pertenece al estado de la aplicación y se
conserva entre módulos. Ofrece valores comunes y un valor personalizado entre
1 y 64; el número de procesadores lógicos disponibles se muestra como dato del
runtime, no como cantidad de núcleos físicos. El número solicitado se aplica a
la visualización y al benchmark, que conserva por separado los hilos efectivos
observados dentro de sus regiones paralelas.

El benchmark compara la misma entrada y generaciones con ambos motores. La
construcción del juego, las comprobaciones de equivalencia y la salida quedan
fuera del intervalo medido; se usa `steady_clock`, calentamiento y mediana de
repeticiones medidas. El resultado final se compara celda por celda. La UI
presenta primero el estado de cálculo, compara las medianas en milisegundos,
muestra speedup/eficiencia con sus unidades e indica que tableros pequeños
pueden no representar cargas mayores.

El benchmark distingue los hilos solicitados de los efectivos. El motor
registra el tamaño del equipo con `omp_get_num_threads()` dentro de cada
región paralela de evolución; se resume el mínimo y máximo observados durante
el calentamiento y las repeticiones. La eficiencia se calcula como
`speedup / hilos efectivos` solo si el conteo se mantuvo estable y los motores
produjeron estados equivalentes. Si varía, la eficiencia se muestra como no
disponible, no como una cifra basada en los hilos solicitados.

Estos patrones pueden almacenarse como archivos de entrada y no sustituyen al módulo `input`.

### renderer

Responsable exclusivamente de representar el estado de la simulación.

Podrá mostrar:

- generación actual;
- construcción de `next`;
- regiones procesadas por hilos;
- regiones MPI;
- intercambio de halos;
- información suministrada por otros módulos.

La simulación debe poder funcionar sin `renderer`.

El renderer no mide rendimiento ni implementa reglas de Conway.

### app / controls

Gestiona el flujo de interacción del usuario.

Debe coordinar:

- menú principal;
- entrada activa;
- acceso a benchmark;
- acceso a visualización;
- selección de implementación;
- cambio de entrada;
- volver y salir.

La visualización podrá incorporar controles opcionales como pausa, avance por generación o velocidad, sin formar parte del núcleo.

### benchmark

Responsable de medir rendimiento sin renderizado.

Cada implementación debe iniciar desde la misma entrada original.

Métricas previstas:

- tiempo total;
- tiempo por generación;
- speedup;
- eficiencia calculada con los hilos OpenMP efectivos, si el equipo es estable;
- throughput.

La validación no debe contaminar la región temporizada del benchmark.

### tests

Contendrá pruebas de corrección.

Las implementaciones OpenMP, Pthreads y MPI deberán compararse con la implementación secuencial.

Casos mínimos:

- Block permanece estable;
- Blinker vuelve a su estado inicial después de dos generaciones;
- OpenMP produce el mismo resultado que secuencial;
- Pthreads produce el mismo resultado que secuencial;
- MPI produce el mismo resultado que secuencial.

---

## 5. Reglas del Juego de la Vida

Cada célula considera sus ocho vecinos.

Para cada generación:

1. Una célula viva con menos de 2 vecinos vivos muere.
2. Una célula viva con 2 o 3 vecinos vivos sobrevive.
3. Una célula viva con más de 3 vecinos vivos muere.
4. Una célula muerta con exactamente 3 vecinos vivos nace.

El estado de la generación `N + 1` debe calcularse únicamente utilizando el estado de la generación `N`.

Se utilizará doble buffer:

```text
current --calcular--> next
                      |
                      +-- swap --> current
```

Todos los motores deben respetar esta regla.

---

## 6. Representación del tablero

El estado del tablero se almacenará mediante memoria contigua.

La representación base utilizará:

```cpp
std::vector<std::uint8_t>
```

Cada posición representa:

```text
0 -> célula muerta
1 -> célula viva
```

El tablero se almacena de forma lineal.

Con halo:

```cpp
stride = width + 2;
index = row * stride + col;
```

Las células reales ocupan:

```text
row = 1 .. height
col = 1 .. width
```

El halo ocupa las filas y columnas exteriores.

Los demás módulos no deben depender directamente de la representación interna de `core`.

---

## 7. Entrada activa

La aplicación mantiene una única entrada activa durante su ejecución.

El flujo general es:

```text
archivo .txt
     |
     v
   input
     |
     v
 InputData
     |
     +--> benchmark
     |
     +--> visualización
```

Si no existe una entrada activa al entrar a un modo, el programa solicitará una.

Si ya existe, se reutilizará.

El usuario podrá cambiarla desde `benchmark` o `visualización`.

Cada ejecución debe crear su propio estado de trabajo a partir de la entrada inicial, sin modificar el original.

---

## 8. Paralelización

### OpenMP

OpenMP distribuirá entre hilos el cálculo de las células.

No debe utilizarse para paralelizar:

- renderizado;
- entrada de usuario;
- lectura de teclado;
- escritura de archivos.

### Pthreads

Pthreads utilizará hilos creados explícitamente.

La primera estrategia será dividir el tablero en rangos de filas.

Cada hilo trabajará sobre una región propia de `next`.

### MPI

La primera estrategia será dividir el tablero horizontalmente:

```text
+----------------------+
|      Proceso 0       |
+----------------------+
|      Proceso 1       |
+----------------------+
|      Proceso 2       |
+----------------------+
|      Proceso 3       |
+----------------------+
```

Los procesos intercambiarán las filas frontera necesarias mediante halos.

---

## 9. Benchmark y validación

Todos los modos deben recibir:

- la misma entrada inicial;
- el mismo número de generaciones;
- las mismas reglas;
- las mismas condiciones de borde.

Durante pruebas de corrección se podrá comparar el tablero generación por generación.

Durante benchmarks, la validación debe realizarse fuera de la región temporizada.

El resultado paralelo debe ser equivalente al resultado secuencial.

---

## 10. Principios de integración

Un módulo no debe conocer detalles que no necesita.

- `core` no depende de MPI.
- `core` no depende de OpenMP.
- `core` no depende de Pthreads.
- `core` no depende de la interfaz.
- `renderer` no implementa reglas de Conway.
- `renderer` no mide rendimiento.
- los motores de ejecución no contienen código de renderizado.
- `benchmark` puede ejecutar la simulación sin interfaz.
- `main` coordina módulos, pero no concentra su implementación.
- `input` no conoce detalles de la representación interna del tablero.
- Los motores (`sequential`, `openmp`, `pthreads`, `mpi`) no duplican la regla B3/S23: la regla vive una sola vez en `core::bs23::next_cell_state(...)`. Cada motor controla su recorrido (secuencial, OpenMP, Pthreads, MPI) y su propia estrategia de `aliveCount`, pero todos invocan `next_cell_state` celda por celda sobre un mismo `GameState`.

No se deben crear versiones alternativas del `core` dentro de otros módulos. En particular: añadir un motor de ejecución significa crear `src/core/engines/<motor>.cpp` con la firma `void step_<motor>(GameState&)`, sin redefinir la regla.

---

## 11. Entorno tecnológico de referencia

El entorno de referencia del proyecto es:

- Ubuntu 24.04 LTS;
- C++20;
- GCC/G++ 14.2;
- CMake 3.20 o superior;
- OpenMP mediante GCC (`libgomp`);
- Open MPI 5.0.10 compilado con GCC 14.

La instalación de referencia de Open MPI se encuentra en:

```text
/opt/openmpi-5.0.10-gcc14
```

El proyecto utiliza CMake para localizar y enlazar:

- `OpenMP::OpenMP_CXX`;
- `MPI::MPI_CXX`.

Las pruebas oficiales de rendimiento deberán ejecutarse en este entorno o documentando cualquier diferencia.

---

## 12. Interfaces y cambios

Las interfaces públicas compartidas forman parte del contrato del proyecto.

Antes de modificar una interfaz utilizada por otros módulos se debe:

1. justificar el cambio;
2. comunicarlo al integrador;
3. revisar los módulos afectados;
4. actualizar este documento si cambia la arquitectura.

No se deben introducir dependencias nuevas sin coordinación.

---

## 13. Decisiones todavía abiertas

Las siguientes decisiones pueden evolucionar:

- detalles de la estrategia MPI;
- biblioteca definitiva para la interfaz de terminal;
- representación visual de hilos y procesos;
- controles interactivos de visualización;
- tamaños finales de benchmark;
- formato definitivo de exportación de resultados.

Estas decisiones no deben asumirse como definitivas sin coordinación con el equipo.

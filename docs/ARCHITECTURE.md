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

Incluye:

- ancho y alto lógicos;
- `stride`;
- buffer `current`;
- buffer `next`;
- generación actual;
- consulta y modificación controlada de células.

El tablero utiliza memoria contigua y un halo de una celda alrededor del área lógica.

Para un tablero lógico:

```text
width x height
```

la representación interna será:

```text
(width + 2) x (height + 2)
```

No debe contener:

- MPI;
- OpenMP;
- Pthreads;
- interfaz de terminal;
- lectura de teclado;
- mediciones de rendimiento.

### sequential

Implementación secuencial de la evolución del tablero.

Es la implementación de referencia para validar OpenMP, Pthreads y MPI.

Debe recorrer únicamente las células reales del tablero y calcular `next` leyendo exclusivamente desde `current`.

### openmp

Implementación de la evolución utilizando memoria compartida mediante OpenMP.

Paralelizará principalmente el cálculo de las células del tablero.

Todos los hilos deben:

- leer desde `current`;
- escribir únicamente en su región correspondiente de `next`;
- sincronizarse antes del intercambio de buffers.

### pthreads

Implementación mediante POSIX Threads.

El programa crea explícitamente los hilos y asigna rangos del tablero a cada uno.

Todos los hilos deben:

- leer desde `current`;
- escribir únicamente en su región correspondiente de `next`;
- sincronizarse antes del intercambio de buffers.

### mpi

Responsable de la ejecución distribuida mediante MPI.

Inicialmente se utilizará una partición horizontal del tablero.

Cada proceso trabajará con una región local y se comunicará con sus procesos vecinos mediante filas halo.

La estrategia podrá revisarse posteriormente si los experimentos justifican otra descomposición.

### patterns

Contendrá entradas conocidas para pruebas y demostraciones, por ejemplo:

- Block;
- Blinker;
- Glider;
- Toad;
- Beacon;
- Pulsar;
- Gosper Glider Gun.

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
- eficiencia;
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

No se deben crear versiones alternativas del `core` dentro de otros módulos.

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

# Arquitectura del proyecto

## 1. Propósito

Este documento define la arquitectura base de `conway-parallel`.

Su objetivo es permitir que distintos integrantes desarrollen módulos en paralelo sin crear implementaciones incompatibles entre sí.

Las decisiones definidas aquí deben respetarse durante el desarrollo. Cualquier cambio que afecte interfaces comunes o la arquitectura debe coordinarse antes de implementarse.

---

## 2. Objetivo técnico

Implementar el Juego de la Vida de Conway en C++ mediante cuatro modos de ejecución:

- Secuencial
- OpenMP
- MPI
- Híbrido MPI + OpenMP

Todos los modos deben producir el mismo estado del tablero para una misma configuración inicial.

La implementación secuencial será utilizada como referencia para validar las implementaciones paralelas.

---

## 3. Arquitectura general

```text
                     main / app
                         |
          +--------------+--------------+
          |              |              |
       controls       renderer       benchmark
          |              |              |
          +--------------+--------------+
                         |
                       CORE
                  estado del tablero
                         |
        +----------------+-----------------+
        |                |                 |
   Sequential         OpenMP              MPI
        |                |                 |
        +----------------+-------+---------+
                                 |
                          MPI + OpenMP
```

---

## 4. Módulos

### core

Responsable únicamente del estado y representación del Juego de la Vida.

Incluye:

- dimensiones del tablero;
- estado de las células;
- edad de las células;
- generación actual;
- consulta y modificación de células.

No debe contener:

- MPI;
- OpenMP;
- interfaz de terminal;
- entrada de teclado;
- mediciones de rendimiento.

### sequential

Implementación secuencial de la evolución del tablero y referencia de corrección para OpenMP, MPI e híbrido.

### openmp

Implementación de la evolución utilizando memoria compartida mediante OpenMP. Paralelizará principalmente el cálculo de las células del tablero.

### mpi

Responsable de la ejecución distribuida mediante MPI. Inicialmente se utilizará una partición horizontal del tablero. Cada proceso trabajará con una región local y se comunicará con los procesos vecinos mediante filas halo.

### hybrid

Combina ambas estrategias:

- MPI distribuye regiones del tablero entre procesos.
- OpenMP paraleliza el cálculo dentro de cada proceso.

### patterns

Responsable de cargar configuraciones conocidas, como Block, Blinker, Glider, Toad, Beacon, Pulsar y Gosper Glider Gun.

### renderer

Responsable exclusivamente de representar el estado de la simulación.

Debe permitir posteriormente:

- visualización por edad de las células;
- visualización de regiones MPI;
- límites entre regiones;
- métricas de ejecución.

La simulación debe poder funcionar sin `renderer`.

### controls

Gestionará acciones del usuario como Play/Pause, Step, Reset, Clear, Random, selección de patrones y cambio de visualización.

### benchmark

Responsable de medir el rendimiento sin renderizado para evitar que la interfaz altere las mediciones.

Métricas previstas:

- tiempo de ejecución;
- tiempo por generación;
- speedup;
- eficiencia;
- throughput.

### tests

Contendrá pruebas de corrección. Las implementaciones OpenMP, MPI e híbrida deberán compararse con la implementación secuencial.

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

---

## 6. Edad de las células

La edad se utilizará únicamente con fines de visualización.

```text
célula muerta        -> edad = 0
célula que nace      -> edad = 1
célula que sobrevive -> edad + 1
célula que muere     -> edad = 0
```

La edad nunca debe modificar las reglas originales de Conway.

---

## 7. Representación del tablero

El estado del tablero se almacenará mediante estructuras contiguas en memoria.

La implementación actual utiliza:

- `std::vector<std::uint8_t>` para representar el estado vivo/muerto;
- `std::vector<std::uint16_t>` para representar la edad de las células.

El tablero bidimensional se representa internamente de forma lineal, utilizando la relación:

`index = row * width + col`

Esta representación facilita recorridos secuenciales y paralelos sobre memoria contigua.

La representación interna permanece encapsulada dentro de `core`. Los demás módulos no deben depender directamente de estas estructuras y deberán acceder al estado mediante las interfaces públicas definidas por `core`.

---

## 8. Paralelización

### OpenMP

OpenMP se utilizará para distribuir entre hilos el cálculo de células.

No debe utilizarse para paralelizar el renderizado, la entrada de usuario, la lectura de teclado o la escritura de archivos.

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

Los procesos intercambiarán las filas necesarias de sus fronteras mediante halos.

Esta estrategia podrá revisarse posteriormente si los experimentos justifican otra descomposición.

### MPI + OpenMP

```text
Tablero
   |
   +-- MPI -> regiones
   |
   +-- cada región
          |
          +-- OpenMP -> hilos
```

---

## 9. Principios de integración

Un módulo no debe conocer detalles que no necesita.

- `core` no depende de MPI.
- `core` no depende de OpenMP.
- `core` no depende de la interfaz.
- `renderer` no implementa reglas de Conway.
- los motores de ejecución no contienen código de renderizado.
- `benchmark` puede ejecutar la simulación sin interfaz.
- `main` coordina módulos, pero no concentra su implementación.

Si aparecen instrucciones MPI, códigos de color o lectura de teclado dentro de `core`, debe revisarse el diseño.

---

## 10. Entorno tecnológico de referencia

El entorno de referencia del proyecto es:

- Ubuntu 24.04 LTS;
- C++20;
- GCC/G++ 14.2;
- CMake 3.20 o superior;
- OpenMP mediante GCC (`libgomp`);
- Open MPI 5.0.10 compilado con GCC 14.

La instalación de referencia de Open MPI se encuentra en:

`/opt/openmpi-5.0.10-gcc14`

El proyecto utiliza CMake para localizar y enlazar OpenMP y MPI mediante:

- `OpenMP::OpenMP_CXX`;
- `MPI::MPI_CXX`.

Las pruebas de rendimiento oficiales del proyecto deberán ejecutarse utilizando este entorno de referencia o documentando explícitamente cualquier diferencia.

---

## 11. Interfaces y cambios

Las interfaces públicas compartidas entre módulos forman parte del contrato del proyecto.

Antes de modificar una interfaz utilizada por otros integrantes se debe:

1. justificar el cambio;
2. comunicarlo al integrador;
3. revisar los módulos afectados;
4. actualizar este documento si cambia la arquitectura.

No se deben crear versiones alternativas del `core` dentro de otros módulos.

---

## 12. Decisiones todavía abiertas

Las siguientes decisiones todavía pueden evolucionar:

- detalles de la estrategia de partición MPI;
- biblioteca definitiva para la interfaz de terminal;
- representación visual de hilos y procesos;
- tamaños y configuraciones finales de los benchmarks.

Estas decisiones no deben asumirse como definitivas sin coordinación con el equipo.
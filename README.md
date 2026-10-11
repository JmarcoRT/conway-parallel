# Conway Parallel

Simulador del **Juego de la Vida de Conway** desarrollado en C++ para estudiar y comparar distintas formas de ejecución:

- Secuencial
- OpenMP
- Pthreads
- MPI

El proyecto busca comprobar que las implementaciones paralelas y distribuidas produzcan los mismos resultados que la versión secuencial, y analizar su rendimiento mediante métricas como tiempo de ejecución, speedup y eficiencia.

Además, el simulador contará con:

- carga de tableros desde archivos `.txt`;
- modo de visualización de la evolución del tablero;
- visualización del trabajo realizado por threads y procesos;
- modo benchmark sin renderizado para realizar mediciones comparables.

## Tecnologías

- C++20
- OpenMP
- Pthreads
- Open MPI
- CMake

Entorno de referencia: **Ubuntu 24.04 LTS**.

## Prerrequisitos e Instalación

En Ubuntu / Debian / WSL:

```bash
sudo apt update
sudo apt install -y libopenmpi-dev openmpi-bin cmake g++
```

## Compilación y Ejecución

```bash
# Configurar el proyecto mediante CMake
cmake -B build -S .

# Compilar todos los objetivos
cmake --build build

# Ejecutar la demostración de la interfaz de terminal (Menú Principal y Layout Base)
./build/conway_ui_demo

# Ejecutar la aplicación interactiva
./build/conway
```

> **Nota:** La interfaz usa caracteres UTF-8 para el tablero y los bordes. El color ANSI se activa cuando la salida está conectada a una terminal compatible; en otros casos se usa una presentación monocromática. Define `NO_COLOR` para desactivar el color.

## Estructura actual

```text
conway-parallel/
├── docs/       # Arquitectura, UI y reglas de colaboración
├── include/    # Cabeceras y APIs
├── patterns/   # Tableros y patrones de entrada
├── src/        # Código fuente
└── tests/      # Pruebas
```

- La arquitectura del sistema está definida en [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
- La documentación del módulo de UI y Layout Base está en [`docs/UI.md`](docs/UI.md).
- Las reglas para contribuir al proyecto están en [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md).

## Estado

Proyecto en desarrollo.

Actualmente se encuentran implementados los motores secuencial y OpenMP, la selección persistente de motor y los flujos funcionales de visualización y benchmark:

- carga y validación de tableros desde archivos `.txt`;
- representación del tablero en memoria contigua;
- halo de una celda alrededor del tablero lógico;
- doble buffer mediante `current` y `next`;
- evolución secuencial de generaciones;
- Menú principal y selección persistente de motor Secuencial/OpenMP;
- visualización interactiva de CURRENT/NEXT con avance, reproducción, pausa y reinicio;
- benchmark reproducible entre motores con mediana, speedup, hilos OpenMP solicitados/efectivos, eficiencia válida y comprobación de equivalencia. La eficiencia no se informa si varía el equipo efectivo.

Pthreads y MPI todavía no están disponibles como motores de la aplicación.

### Controles para una demostración

- En el menú, selecciona `4` para configurar OpenMP: 1, 2, 4, 8 o un valor
  personalizado entre 1 y 64. La opción se conserva durante la sesión y se
  aplica a visualización y benchmark.
- La aplicación informa los procesadores lógicos disponibles; no los presenta
  como núcleos físicos. El equipo efectivo puede ser menor al solicitado por
  límites del runtime o del entorno.
- En visualización: `N` avanza, espacio pausa/reanuda, `R` reinicia y `Esc`
  vuelve al menú. La pantalla pausada no se vuelve a imprimir sin cambios.
- La cuadrícula y el marco se adaptan al ancho del terminal y limitan las celdas visibles; las
  dimensiones omitidas se indican. Los colores se usan en terminales ANSI y
  pueden desactivarse estableciendo `NO_COLOR`.
- El benchmark presenta medianas en milisegundos, speedup, eficiencia cuando
  el equipo efectivo es estable y una advertencia para tableros pequeños.

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

## Estructura actual

```text
conway-parallel/
├── docs/       # Arquitectura y reglas de colaboración
├── include/    # Cabeceras y APIs
├── patterns/   # Tableros y patrones de entrada
├── src/        # Código fuente
└── tests/      # Pruebas
```

La arquitectura del sistema está definida en [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

Las reglas para contribuir al proyecto están en [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md).

## Estado

Proyecto en desarrollo.

Actualmente se encuentra implementada la base del motor secuencial, incluyendo:

- carga y validación de tableros desde archivos `.txt`;
- representación del tablero en memoria contigua;
- halo de una celda alrededor del tablero lógico;
- doble buffer mediante `current` y `next`;
- evolución secuencial de generaciones;
- validación básica mediante patrones conocidos como Blinker.

Las versiones OpenMP, Pthreads y MPI se integrarán progresivamente sobre esta base común.

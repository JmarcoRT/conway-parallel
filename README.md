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
```

> **Nota:** La interfaz requiere una terminal con soporte para secuencias de colores ANSI y UTF-8 (ej. WSL, Linux/macOS Terminal, Windows Terminal moderno).

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

Actualmente se encuentra implementada la base del motor secuencial y la interfaz de usuario:

- carga y validación de tableros desde archivos `.txt`;
- representación del tablero en memoria contigua;
- halo de una celda alrededor del tablero lógico;
- doble buffer mediante `current` y `next`;
- evolución secuencial de generaciones;
- Menú Principal v1 y Layout Base reutilizable en la terminal (S5).

Las versiones OpenMP, Pthreads y MPI se integrarán progresivamente sobre esta base común.

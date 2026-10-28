<h1 align="center">Conway Parallel</h1>

<p align="center">
  Simulación y análisis del Juego de la Vida de Conway mediante programación paralela.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-64748b?style=flat" alt="C++20">
  <img src="https://img.shields.io/badge/CMake-3.20%2B-64748b?style=flat" alt="CMake 3.20 o superior">
</p>

<p align="center">
  Proyecto académico de Ingeniería de Software<br>
  Universidad Nacional Mayor de San Marcos · UNMSM
</p>

## Acerca del proyecto

Conway Parallel es un proyecto académico que utiliza el Juego de la Vida de
Conway para implementar, visualizar y comparar distintos modelos de programación
paralela. Permite explorar el comportamiento de la simulación y evaluar su
rendimiento mediante experimentos reproducibles.

## Características

- **Simulación de Conway:** carga de patrones y evolución del tablero.
- **Catálogo interactivo:** explorar y reproducir en Secuencial, confirmar explícitamente la entrada inicial.
- **Modo didáctico:** representación del trabajo realizado por los hilos.
- **Comparación experimental:** tiempos, estadísticas y equivalencia entre motores.
- **Escalabilidad:** análisis de OpenMP con distintas cantidades de hilos.
- **Generación reproducible:** tableros definidos por dimensiones, densidad y semilla.

## Modelos de ejecución

| Modelo | Estado |
|---|---|
| Secuencial | Implementado |
| OpenMP | Implementado |
| MPI | Simulación local con procesos separados (Linux/WSL2) |
| Pthreads | Pendiente |

## Inicio rápido

Para compilar y ejecutar: Linux/WSL, un compilador C++20 con OpenMP,
CMake ≥ 3.20, MPI C++ y Git. La configuración actual requiere MPI incluso para
la aplicación local. CMake descarga FTXUI en la primera configuración; se necesita
acceso a red o una copia local verificada.

Python 3 se requiere cuando se habilitan las pruebas, como en los comandos
siguientes y en el preset Release.

**Desarrollo**

Desde la raíz del repositorio, configura, compila y ejecuta:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
./build/conway
```

**Release**

Para experimentos, el preset requiere G++ 14 en `/usr/bin/g++-14`:

```bash
cmake --preset experimental-release
cmake --build --preset experimental-release
./build-release/conway
```

En el menú, `C` abre el catálogo de patrones. Usa flechas y Enter para navegar.
Consulta los [controles](docs/UI.md), los [requisitos avanzados de compilación
y experimentación](docs/BENCHMARK_PROTOCOL.md) y la [guía de pruebas](docs/TESTING.md).

## Estructura del repositorio

```text
conway-parallel/
├── include/        # Cabeceras y contratos
├── src/            # Implementación
├── tests/          # Pruebas automatizadas
├── patterns/       # Tableros de ejemplo
├── docs/           # Guías y documentación técnica
├── scripts/        # Herramientas auxiliares
└── CMakeLists.txt  # Configuración de compilación
```

## Documentación

- [Arquitectura](docs/ARCHITECTURE.md)
- [Interfaz y controles](docs/UI.md)
- [Pruebas y validación](docs/TESTING.md)
- [Protocolo experimental](docs/BENCHMARK_PROTOCOL.md)
- [Índice de documentación](docs/README.md)
- [Guía de contribución](docs/CONTRIBUTING.md)

## Equipo

| Integrante | Integrante |
|---|---|
| Ryan Bernal | Manuel Guzmán |
| Iris Hanampa | Jeanmarco Rosales |
| Álvaro Díaz | Ronald Munayco |
| Valeria Ccahuana | Claudia Bautista |

---

<p align="center">Conway Parallel · UNMSM · 2026</p>

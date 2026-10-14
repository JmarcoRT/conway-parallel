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

Conway Parallel explora distintos modelos de ejecución para simular y analizar
el Juego de la Vida. Este autómata celular permite observar cómo reglas sencillas
producen patrones complejos a lo largo de generaciones.

El proyecto utiliza ese mismo problema para estudiar programación paralela:
comparar resultados, visualizar el trabajo y analizar cómo cambia el rendimiento
al distribuir el cómputo. Su alcance contempla memoria compartida y distribuida,
con implementaciones en distintas etapas de desarrollo.

## Características

- **Simulación de Conway:** carga de patrones y evolución del tablero.
- **Visualización interactiva:** avance, pausa, reproducción y reinicio.
- **Modo didáctico:** representación del trabajo realizado por los hilos.
- **Comparación experimental:** tiempos, estadísticas y equivalencia entre motores.
- **Escalabilidad:** análisis de OpenMP con distintas cantidades de hilos.
- **Generación reproducible:** tableros definidos por dimensiones, densidad y semilla.

## Modelos de ejecución

| Modelo | Estado |
|---|---|
| Secuencial | Implementado |
| OpenMP | Implementado |
| MPI | Prototipo independiente |
| Pthreads | Pendiente |

## Demostración

Las capturas reales de la aplicación están pendientes de incorporar. Esta sección
mostrará la simulación, el modo didáctico y los resultados experimentales.

## Inicio rápido

Requisitos: Linux/WSL, compilador C++20 con OpenMP, CMake ≥ 3.20, MPI C++, Git y
Python 3 para las pruebas. CMake obtiene FTXUI en la primera configuración;
se necesita acceso a red o una copia local verificada. Más detalles en el
[protocolo de compilación y experimentos](docs/BENCHMARK_PROTOCOL.md).

Desde la raíz del repositorio, configura, compila y ejecuta:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
./build/conway
```

Para una compilación **Release** destinada a experimentos, el preset requiere
G++ 14 en `/usr/bin/g++-14`:

```bash
cmake --preset experimental-release
cmake --build --preset experimental-release
./build-release/conway
```

En el menú, `C` abre el catálogo de patrones. Usa flechas y Enter para navegar.
Consulta los [controles de la interfaz](docs/UI.md) y la [guía de pruebas](docs/TESTING.md).

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

Proyecto académico desarrollado en la UNMSM. La documentación versionada
referencia aportes de **Álvaro** y **R. Munayco**. La nómina completa y los nombres
completos de los integrantes están pendientes de confirmar para esta portada.

---

<p align="center">Conway Parallel · UNMSM · 2026</p>

# Conway Parallel

Simulador del **Juego de la Vida de Conway** desarrollado en C++ para estudiar y comparar distintas formas de ejecución:

- Secuencial
- OpenMP
- MPI
- MPI + OpenMP

El proyecto busca comprobar que las versiones paralelas produzcan los mismos resultados que la versión secuencial y analizar su rendimiento.

## Tecnologías

- C++17
- OpenMP
- Open MPI
- CMake

Entorno de referencia: **Ubuntu 24.04 LTS**.

## Estructura actual

```text
conway-parallel/
├── docs/       # Arquitectura y reglas de colaboración
├── include/    # Cabeceras
├── src/        # Código fuente
└── tests/      # Pruebas
```

La arquitectura del sistema está definida en [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

Las reglas para contribuir al proyecto están en [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md).

## Estado

Proyecto en desarrollo.
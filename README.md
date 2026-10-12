# Conway Parallel

Simulador del **Juego de la Vida de Conway** desarrollado en C++ para estudiar y comparar distintas formas de ejecución:

- Secuencial
- OpenMP
- Pthreads
- MPI

El proyecto busca comprobar que las implementaciones paralelas y distribuidas produzcan los mismos resultados que la versión secuencial, y analizar su rendimiento mediante métricas como tiempo de ejecución, speedup y eficiencia.

La aplicación interactiva ofrece:

- carga de tableros desde archivos `.txt`;
- catálogo dinámico de patrones `.txt` en `patterns/`, además de carga manual;
- visualización adaptativa de CURRENT/NEXT con avance, pausa, reinicio y selección persistente de motor;
- configuración OpenMP por sesión y benchmark sin renderizado entre Secuencial/OpenMP, con medianas, speedup, eficiencia válida y verificación de equivalencia.

La visualización detallada del trabajo de threads y procesos está prevista para
cuando esos motores se integren en la aplicación.

## Tecnologías

- C++20
- OpenMP
- Pthreads
- Open MPI
- CMake
- FTXUI v5.0.0 (pinned commit, fetched by CMake)

Entorno de referencia: **Ubuntu 24.04 LTS**.

## Prerrequisitos e Instalación

En Ubuntu / Debian / WSL:

```bash
sudo apt update
sudo apt install -y libopenmpi-dev openmpi-bin cmake g++ git
```

## Compilación y Ejecución

```bash
# Configurar el proyecto mediante CMake
cmake -B build -S .

# Compilar todos los objetivos
cmake --build build

# Ejecutar la aplicación interactiva
./build/conway

# Ejecutar una demo breve de CURRENT/NEXT con Blinker
./build/conway_view_demo patterns/blinker.txt 2
```

> **Nota:** En la primera configuración, CMake descarga FTXUI v5.0.0 desde un commit fijado. En una TTY, la aplicación usa la pantalla alternativa, actualiza únicamente las filas modificadas y restaura el estado de la terminal al salir. Un cambio de dimensiones fuerza un repintado completo para borrar residuos. El layout se adapta al tamaño reportado por Linux/WSL. Las celdas usan bloques UTF-8; `NO_COLOR=1` desactiva el color. Se requiere una terminal con soporte UTF-8 y secuencias ANSI de control.

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
- La documentación de la interfaz FTXUI está en [`docs/UI.md`](docs/UI.md).
- El formato de entrada y los patrones de demo están en [`docs/INPUT_TESTS.md`](docs/INPUT_TESTS.md).
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

- En el menú, usa flechas y Enter o los atajos visibles. `C` abre el catálogo
  dinámico de `patterns/*.txt`; selecciona una entrada con flechas y Enter.
  Los archivos `.txt` nuevos aparecen automáticamente. La opción de ruta
  manual conserva la compatibilidad con patrones fuera de ese directorio.
- En el menú, selecciona `4` para configurar OpenMP: 1, 2, 4, 8 o un valor
  personalizado entre 1 y 64. La opción se conserva durante la sesión y se
  aplica a visualización y benchmark.
- La aplicación informa los procesadores lógicos disponibles; no los presenta
  como núcleos físicos. El equipo efectivo puede ser menor al solicitado por
  límites del runtime o del entorno.
- En visualización: `N` avanza, espacio pausa/reanuda, `R` reinicia y `Esc`
  vuelve al menú. La reproducción publica eventos temporizados; en pausa no hay
  actualizaciones periódicas. El render diferencial escribe solo las filas
  modificadas.
- La cuadrícula y los paneles CURRENT/NEXT se centran y ajustan al tamaño del
  terminal. Los tableros grandes usan un viewport y declaran las filas o
  columnas no mostradas; las células no se recortan silenciosamente.
- Si la aplicación recibe entrada por pipe (no TTY), mantiene la carga de ruta
  por línea para permitir automatización y scripts existentes.
- El benchmark presenta medianas en milisegundos, speedup, eficiencia cuando
  el equipo efectivo es estable y una advertencia para tableros pequeños.

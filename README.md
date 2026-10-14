# Conway Parallel

Simulador del Juego de la Vida de Conway en C++20 para estudiar ejecución
secuencial y paralela con OpenMP. Entorno de referencia: Ubuntu/WSL Linux,
G++ 14 y Windows Terminal con UTF-8 y secuencias ANSI.

## Funcionalidades implementadas

- Carga validada de tableros `.txt`, catálogo dinámico y ruta manual.
- Motores Secuencial/OpenMP con regla B3/S23 compartida y borde muerto fijo.
- Simulación CURRENT/NEXT: paso, reproducción, pausa y reinicio.
- Visualización didáctica de una generación mediante StepTrace opcional.
- Comparación configurable de motores y escalabilidad fuerte OpenMP desde FTXUI.
- Entradas experimentales cargadas o generadas reproduciblemente con semilla.
- Muestras originales, estadísticas poblacionales, equivalencia, speedup y
  eficiencia con equipos efectivos observados; vistas Resumen/Muestras/Método.
- Captura de metadatos y exportación CSV como API, no como acción FTXUI.

MPI es un **prototipo independiente de distribución/reensamblado**, sin evolución
ni intercambio de halos. El motor Pthreads está pendiente. Los workers de UI
usan `std::thread`; no constituyen ese motor. La sesión conserva un último
resultado, no un historial persistente.

## Dependencias y compilación

En Ubuntu/Debian/WSL se requieren CMake >= 3.20, un compilador C++20 con OpenMP,
MPI C++, Git y Python 3 para las pruebas. Ejemplo de instalación:

```bash
sudo apt update
sudo apt install -y cmake g++ git python3 libopenmpi-dev openmpi-bin
```

El preset experimental requiere además `/usr/bin/g++-14`; instalar G++ 14 según
la distribución. FTXUI v5.0.0 se obtiene con FetchContent desde el commit fijado
`cdf28903a7781f97ba94d30b79c3a4b0c97ccce7`. La primera configuración necesita red
salvo que se suministre una copia local verificada. MPI es requerido actualmente
por CMake aunque no se seleccione el prototipo. No se acredita soporte Windows
nativo: los controles y pruebas PTY usan APIs POSIX dentro de Linux/WSL.

Desarrollo, desde la raíz:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
./build/conway
```

Build optimizado para experimentos:

```bash
cmake --preset experimental-release
cmake --build --preset experimental-release
ctest --preset experimental-release
```

El preset usa G++ 14, `build-release/`, Release y exporta los comandos de
compilación. Verificar flags efectivos y snapshot antes de medir; el nombre
Release no sustituye esa evidencia. Véase [protocolo](docs/BENCHMARK_PROTOCOL.md).

## Uso

- Menú: flechas/Enter; `C` catálogo, `1` Benchmark, `2` simulación, `3` motor,
  `4` hilos; `0`/`Q` salir. El modo didáctico tiene su entrada en el menú.
- Simulación: `N` paso, espacio/`P` pausa o reproducción, `R` reinicio, `Esc` volver.
- Didáctica: `N` siguiente generación, espacio reproducción, `R` reinicio,
  `+`/`-` velocidad, `Esc` volver. La reproducción es relativa, no cronología real.
- Formularios experimentales: Tab/Mayús+Tab campos, Enter activar. La entrada
  generada permite ancho, alto, densidad y semilla; no cambia el patrón de sesión.
- Resultados: ←→ Resumen/Muestras/Método, ↑↓ desplazamiento, Home/End extremos,
  Esc configuración o submenú según procedencia.

Ejecutar desde la raíz permite descubrir `patterns/`. El formato comienza con
**alto ancho**, seguido de filas binarias; ver [entradas](docs/INPUT_TESTS.md).
La interfaz conserva encabezado contextual, viewport para tableros grandes y
renderizado diferencial. `NO_COLOR=1` desactiva color. En stdin/stdout no TTY se
usa el recorrido textual compatible, que no ofrece todos los flujos FTXUI.

## Pruebas y documentación

```bash
env -u NO_COLOR ctest --test-dir build --output-on-failure
ctest --preset experimental-release
./build/input_tests "$PWD" --dimension-regressions
```

La suite incluye las regresiones de dimensiones, equivalencia Secuencial/OpenMP,
contratos experimentales, CSV y PTY. No exige aceleración ni umbrales de tiempo.

- [Índice de documentación](docs/README.md)
- [Arquitectura vigente](docs/ARCHITECTURE.md)
- [Interfaz](docs/UI.md)
- [Pruebas y sanitización](docs/TESTING.md)
- [Contribución](docs/CONTRIBUTING.md)

Las evidencias exploratorias históricas no constituyen una campaña experimental
actual ni justifican por sí solas causas de rendimiento.

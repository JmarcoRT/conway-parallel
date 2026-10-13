# Protocolo experimental del benchmark

Estado: Fase 2A. Este documento fija el contrato para los experimentos y
separa el comportamiento implementado de las extensiones pendientes. No
modifica `runBenchmark()`, los motores ni la interfaz.

## Compilación experimental

Requisitos existentes: CMake >= 3.20, G++ 14 en `/usr/bin/g++-14`, OpenMP,
MPI, FTXUI y Python 3 para las pruebas. El preset está dirigido a WSL/Linux.

Desde la raíz del proyecto:

```bash
cmake --preset experimental-release
cmake --build --preset experimental-release
ctest --preset experimental-release
```

El preset usa `build-release/`, `Release`, C++20 y dos trabajos de
compilación. Exporta `build-release/compile_commands.json`. No cambia
`build/`, ni fuerza flags globales o específicos de CPU. CMake proporciona
las optimizaciones Release de GCC, normalmente `-O3 -DNDEBUG`; los motores
comparten la configuración de su target y enlazan OpenMP (`-fopenmp`).

Configuración equivalente sin presets:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-14 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_TESTING=ON
cmake --build build-release -j2
env -u NO_COLOR ctest --test-dir build-release --output-on-failure
```

El preset fija `NO_COLOR` vacío solo en los procesos de prueba, habilitando
los colores según el contrato actual de la aplicación. La suite PTY comprueba
RGB y también ejecuta casos explícitos con `NO_COLOR=1`. El comando manual
retira la variable con `env -u`. Ninguno cambia el entorno del usuario.

La primera configuración obtiene la dependencia FTXUI ya declarada en
`CMakeLists.txt`. Sin red, puede reutilizarse su fuente local, tras comprobar
que está en el commit fijado `cdf28903a7781f97ba94d30b79c3a4b0c97ccce7`:

```bash
git -C build/_deps/ftxui-src rev-parse HEAD
cmake --preset experimental-release \
  -DFETCHCONTENT_SOURCE_DIR_FTXUI="$PWD/build/_deps/ftxui-src"
```

Esto comparte únicamente las fuentes; los objetos y bibliotecas Release se
compilan en `build-release/`. Para una compilación limpia se parte de un
directorio experimental inexistente o se archiva el anterior antes de
configurar; no se reutiliza el cache de desarrollo.

## Comprobar y registrar la compilación real

`Release` en el cache no basta. Revisar los comandos que generan los objetos:

```bash
/usr/bin/g++-14 --version
cmake --version
cat build-release/CMakeFiles/conway.dir/flags.make
python3 - <<'PY'
import json
from pathlib import Path

commands = json.loads(Path('build-release/compile_commands.json').read_text())
for entry in commands:
    command = entry['command']
    if ('CMakeFiles/conway.dir/' in command and
        entry['file'].endswith(('src/core/engines/sequential.cpp',
                              'src/core/engines/openmp.cpp',
                              'src/app/benchmark.cpp'))):
        print(command)
PY
```

Confirmar en ambos motores el mismo nivel de optimización efectivo,
`-std=c++20`, `-fopenmp` y el compilador G++ 14. Con GCC y esta configuración
se espera `-O3 -DNDEBUG`. Revisar todo el comando: un `-O0` posterior puede
anular un `-O3` anterior. No usar `-march=native` por defecto ni mezclar
objetos de desarrollo, sanitizadores, cobertura o profiling con la campaña
de rendimiento. Un entorno con `CXXFLAGS` o un cache modificado requiere
esta misma inspección; el preset no certifica por sí solo esos overrides.

Conservar para cada campaña los comandos efectivos, cache, versión del
compilador, versión de CMake, commit del proyecto, estado del árbol y fuente
FTXUI utilizada. No comparar tiempos de desarrollo y Release como si fueran
mediciones bajo condiciones equivalentes. La Fase 2A genera los registros
de compilación; la exportación automatizada de metadatos está pendiente.

## Contrato experimental

### Entrada y unidad de trabajo

- Todos los motores reciben exactamente el mismo tablero inicial, con
  dimensiones y contenido verificables y las mismas reglas y bordes muertos.
- Cada repetición crea su juego desde esa entrada original, en generación
  cero; no continúa el tablero de la repetición anterior.
- Una muestra representa el tiempo total de un lote de `G` generaciones.
  La unidad actual es **milisegundos por lote**, no por generación.
- Si se informa `tiempo/G`, debe identificarse como promedio amortizado por
  generación, no como una muestra individual de cada generación.
- Al integrar el generador, registrar ancho, alto, densidad solicitada y
  observada, semilla, algoritmo/versionado y una identidad estable del contenido.
  Las entradas deberán validarse antes de reservar memoria, incluyendo halos.

### Intervalo cronometrado

- Usar `std::chrono::steady_clock` para tiempo de pared.
- Incluir las llamadas a `Game::step()` y sus costos propios: cálculo,
  actualización de buffers, conteo de vivas y coordinación/sincronización
  OpenMP. No asumir que cada región crea hilos nuevos del sistema operativo.
- Excluir carga o generación, construcción inicial del juego, renderizado,
  exportación, cálculo de estadísticas y validación posterior.
- No activar un sink `StepTrace` en las mediciones oficiales. Las pruebas
  de instrumentación siguen disponibles como validación funcional.
- Documentar cualquier observación incluida: el benchmark actual consulta
  el tamaño efectivo del equipo después de cada paso OpenMP dentro del
  intervalo y acumula su mínimo/máximo.
- Separar calentamientos de muestras medidas: sus tiempos no forman parte
  de las estadísticas. Evitar actividad concurrente ajena al experimento;
  el renderizado fuera del intervalo todavía puede competir por recursos.

### Comparación y resultados

- Comparar la misma entrada y el mismo `G`. Registrar motor y, para OpenMP,
  hilos solicitados y equipo efectivo observado dentro de la región paralela.
- Los hilos efectivos no son una inferencia del número de CPU ni una medida
  de núcleos físicos ocupados. Un equipo puede contener trabajadores sin filas.
- Verificar fuera del cronómetro dimensiones, generación, células y conteo
  de vivas. Un resultado no equivalente **no es válido** para conclusiones
  de rendimiento, aunque exista un cociente numérico de tiempos.
- Usar la mediana como resumen principal. Speedup = mediana secuencial /
  mediana OpenMP. Eficiencia = speedup / hilos efectivos cuando el conteo es
  válido y estable y los estados son equivalentes; mostrar porcentaje solo
  al presentar el resultado.
- Identificar resolución insuficiente y métricas no disponibles. No interpretar
  un cero utilizado como centinela como una aceleración física válida.
- Registrar condiciones del entorno: SO/kernel y WSL, CPU y recursos visibles,
  runtime OpenMP, límites, ajuste dinámico, afinidad y variables `OMP_*`
  relevantes. La topología visible en WSL no certifica la topología física.
- No exigir speedup > 1 ni imponer umbrales de rendimiento para aprobar
  pruebas funcionales. Una ejecución correcta puede favorecer al secuencial.

## Implementado y pendiente

| Aspecto | Implementación actual | Pendiente |
|---|---|---|
| Entradas generadas | Módulo independiente determinista v1, metadatos y validación de memoria; ver [GENERATED_INPUT.md](GENERATED_INPUT.md) | Integración experimental y registro de identidad de entrada |
| Motores | Secuencial y OpenMP siempre se comparan, independientemente del motor seleccionado | Estudio de varios números de hilos |
| Entrada | El mismo `InputData` inicial para cada ejecución | Integración del generador, identidad y parámetros registrados |
| Configuración | API recibe generaciones, repeticiones e hilos; UI usa 20 generaciones y 5 repeticiones | Configuración experimental de esos valores y calentamientos |
| Calentamiento | Uno por motor; su tiempo se descarta. Además se calcula una referencia secuencial previa | Política configurable y registro separado |
| Cronometraje | Construcción, E/S, validación y renderizado fuera; sin sink StepTrace | Mantener este contrato en la extensión |
| Orden | Serie secuencial completa antes de la serie OpenMP | Orden equilibrado y reproducible por rondas |
| Estadística | Mediana; promedio de centrales si el número de muestras es par | Conservar todas las muestras, orden y dispersión |
| Hilos | Solicitados y mínimo/máximo observados; el resumen incluye calentamiento | Separar observaciones de calentamiento y muestras |
| Equivalencia | Comparación final celda por celda fuera del tiempo | Mantener validación y bloquear toda interpretación de resultados no equivalentes |
| Métricas | Speedup; eficiencia válida solo con equipo estable, tiempo OpenMP positivo y equivalencia | Validez explícita para todos los resultados y resolución insuficiente |
| Entorno | Guard RAII restaura hilos y ajuste dinámico del contexto llamante | Registro completo de condiciones y compilación |
| Salida | Resumen en UI; no conserva muestras en `BenchmarkResult` | Exportación de muestras, resúmenes y metadatos |

El código actual todavía calcula speedup aunque falle la equivalencia y
FTXUI puede añadir una interpretación de rapidez junto al error. Este
protocolo exige descartar esa conclusión; corregir su presentación/validez
corresponde a una fase posterior. También quedan pendientes la validación
binaria general de entradas en memoria (el generador sí produce valores binarios)
y los defectos del cargador documentados en
`INPUT_TESTS.md`; la Fase 2A no los corrige.

## Validación y siguientes fases

```bash
ctest --preset experimental-release
cmake --build build -j2
env -u NO_COLOR ctest --test-dir build --output-on-failure
```

CTest incluye equivalencia Secuencial/OpenMP con 1/2/4/8 hilos, restricciones
de equipo efectivo, métricas del benchmark, instrumentación opcional y flujos
PTY. Estas pruebas verifican corrección y regresiones, no una aceleración
esperada. La suite opcional `input_tests --dimension-regressions` contiene
tres defectos conocidos fuera del CTest normal; véase `INPUT_TESTS.md`.

La herramienta `tests/openmp_timing.cpp` es exploratoria y distinta del
benchmark de la aplicación. `scripts/measure_openmp.sh` compila con `-O2`
y `${CXX:-g++}`; sus resultados no se mezclan automáticamente con esta
configuración G++ 14 Release.

Fase 2B: generador reproducible implementado como módulo independiente.
Fases siguientes: integración experimental, conservación de muestras,
orden equilibrado, comparación de escalabilidad con referencia secuencial
compartida por estudio/bloque y exportación. Los parámetros de campaña y
políticas correspondientes requieren revisión antes de implementarse.

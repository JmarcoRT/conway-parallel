# Protocolo experimental del benchmark

Estado: Fase 2A. Este documento fija el contrato para los experimentos y
separa el comportamiento implementado de las extensiones pendientes. No
modifica `runBenchmark()`, los motores ni la interfaz.

El ejecutor independiente de Fase 4 se documenta en
[EXPERIMENT_RUNNER.md](EXPERIMENT_RUNNER.md). Las descripciones históricas de
orden, calentamientos y resultados de este documento corresponden a
`runBenchmark()`; su procedimiento permanece sin cambios. El estudio de
escalabilidad fuerte de Fase 5 se documenta en [SCALING_STUDY.md](SCALING_STUDY.md).

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

## Procedimientos vigentes y alcance

Hay dos contratos distintos; no mezclar sus tiempos o resúmenes:

| Aspecto | Benchmark histórico `runBenchmark()` | Ejecutores configurables |
|---|---|---|
| Consumidor | Consola y compatibilidad histórica | Formularios FTXUI y API |
| Entrada | InputData prestado | Patrón o entrada generada una vez antes de medir |
| Orden | Serie secuencial, después OpenMP | Comparación alternada; escalabilidad por rotación |
| Calentamiento | Uno por motor | Configurable por motor/configuración |
| Referencia | Oráculo previo, excluido de muestras | Oráculo previo; escalabilidad comparte una muestra S por ronda |
| Muestras | Originales, S seguido de O | Originales con ronda, posición y ejecución |
| Equipos resumidos | Incluyen calentamiento histórico | Solo muestras medidas |
| Estadísticas | Mediana y descriptivas de muestras medidas | Mín/máx/mediana/media/DE poblacional |
| Métricas | Campos y fórmulas históricos conservados | Optional; comparación válida y medianas positivas |

`runOnce()` construye Game y aplica configuración antes de `steady_clock`.
El intervalo cubre el lote de generaciones mediante `Game::step()` sin sink,
incluidas sincronización interna y consultas del equipo observado después de
cada paso OpenMP. Construcción, generación/copia, identidad, comparación final,
registro, estadísticas, metadatos, CSV y render quedan fuera.

La validación de equivalencia compara dimensiones, generación, población y todas
las células fuera del reloj. En ejecutores configurables, speedup es mediana S /
mediana O; eficiencia es speedup / equipo efectivo estable. La pantalla la expresa
en porcentaje, el backend conserva fracción. No usar hilos solicitados como
sustituto ni convertir métricas ausentes en cero. El benchmark histórico conserva
su cálculo de speedup aunque falle equivalencia; no debe interpretarse ese valor
como evidencia de rendimiento válido. Véase [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md).

## Implementado y límites

Implementados: generación determinista v1, identidad, configuración, muestras,
estadísticas, comparación alternada, escalabilidad con referencia compartida,
metadatos, CSV por API y presentación FTXUI. Contratos detallados:
[EXPERIMENT_RUNNER.md](EXPERIMENT_RUNNER.md), [SCALING_STUDY.md](SCALING_STUDY.md),
[EXPERIMENT_CSV.md](EXPERIMENT_CSV.md).

La UI congela solicitud y mantiene resultados/contexto juntos. No tiene exportación
interactiva ni historial persistente. Un worker experimental activo bloquea otros
cálculos de simulación/didáctica/catálogo; el render sigue fuera del intervalo,
pero puede competir por recursos. El estudio no elimina efectos de caché,
frecuencia, afinidad, carga del host o planificación. No atribuirles una diferencia
observada sin evidencia adicional ni afirmar significancia a partir de medianas.

El generador y escalabilidad tienen presupuestos explícitos de payload; no son
límites de RSS ni garantía de RAM. El lector y Game ahora validan dimensiones,
productos, halo y células antes de reservas relevantes. Los fallos de asignación
siguen siendo posibles. No se capturan trazas didácticas durante los experimentos;
sus intervalos tienen otro contrato: [STEP_TRACE.md](STEP_TRACE.md).

## Procedencia y validación

Reconstruir desde el estado que se quiera medir y verificar el snapshot generado:
Git HEAD/dirty corresponde al build, no al momento de ejecutar. No actualizarlo
manualmente ni capturar un contexto nuevo al exportar resultados antiguos.
Los comandos capturados describen recetas CMake, no certifican launchers externos.

```bash
cmake --build --preset experimental-release
ctest --preset experimental-release
env -u NO_COLOR ctest --test-dir build --output-on-failure
./build/input_tests "$PWD" --dimension-regressions
```

CTest verifica equivalencia y contratos, no rendimiento mínimo. Las pruebas grandes
funcionales no son campañas certificadas. `scripts/measure_openmp.sh` y
`tests/openmp_timing.cpp` son herramientas exploratorias con compilación y método
propios; no mezclar sus datos con este protocolo. Las evidencias anteriores están
clasificadas en el [índice](README.md).

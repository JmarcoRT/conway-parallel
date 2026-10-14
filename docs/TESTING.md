# Pruebas y validación vigentes

La suite comprueba corrección y contratos, no aceleración ni tiempos máximos de
rendimiento. Los timeouts PTY son límites contra bloqueos. La evidencia de Semana
4 se conserva en [TESTING_EVIDENCE.md](TESTING_EVIDENCE.md), no describe HEAD.

## Compilación y suite completa

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
env -u NO_COLOR ctest --test-dir build --output-on-failure
cmake --preset experimental-release
cmake --build --preset experimental-release
ctest --preset experimental-release
```

Dependencias: C++20/OpenMP, MPI C++, CMake, Git, FTXUI fijado y Python 3.
Las PTY requieren Linux/WSL y terminal virtual; no son pruebas Windows nativas.
El preset exige G++ 14 en `/usr/bin/g++-14`. Ejecutar desde la raíz del repositorio.

## Cobertura

| Grupo CTest | Contrato principal |
|---|---|
| input_functional, input_dimension_regressions | Formato, cargas, signo/rango/productos/halo, Game exacto y binario |
| rule_bs23_per_cell, conway_correctness | Regla, patrones, borde muerto y estados completos |
| game_openmp_vs_sequential, openmp_vs_sequential | Equivalencia de motores y cambios de motor |
| openmp_equivalence_files | Equivalencia secuencial vs OpenMP por volcados de tablero con 1, 2, 4 y 8 hilos; [OPENMP_VALIDATION.md](OPENMP_VALIDATION.md) |
| generated_input | Casos dorados, rechazo, semilla cero, límites y compatibilidad |
| benchmark_statistics, app_runtime y thread_limit | Estadísticas, benchmark histórico y equipos reales |
| experiment_runner y thread_limit | Alternancia, muestras, validez, restauración y referencia |
| scaling_study y thread_limit | Rotación, referencia compartida, equipos y presupuesto |
| experiment_execution, experiment_form | Solicitudes congeladas, errores, concurrencia y validación |
| experiment_csv, experiment_csv_roundtrip | Precisión, orden, metadatos, escape y fallos de escritura |
| step_trace, didactic_session, didactic_layout | Cobertura, equivalencia, publicación y reproducción relativa |
| comparison_view, scaling_view | Presentación sintética, N/D, orden y geometría |
| app_functional_smoke, engine_selection_smoke, ftxui_*_pty | Flujos reales, foco, tamaños y render diferencial |
| mpi_partition_unit | Partición y estado local; no evolución MPI |
| openmp_timing_smoke | Ejecución exploratoria correcta; no certifica rendimiento |

`ctest --test-dir build -N` enumera los nombres registrados. Las pruebas de
limitación de equipo no confunden solicitudes con tamaños efectivos.

## Entradas y regresiones

```bash
./build/input_tests "$PWD"
./build/input_tests "$PWD" --dimension-regressions
ctest --test-dir build -R '^input_' --output-on-failure
```

Las tres regresiones antes pendientes están en CTest normal: dimensiones
negativas y producto desbordado deben rechazarse explícitamente. Game también
rechaza cantidad incorrecta, células no binarias y dimensiones/halos fuera de
rango antes de reservar. Los límites representables no garantizan RAM disponible.
Formato y casos: [INPUT_TESTS.md](INPUT_TESTS.md).

Equivalencia significa dimensiones, generación, población y células iguales;
un checksum o población aislados no sustituyen comparación del tablero.
Block/Blinker/Glider tienen oráculos en input_tests; Toad/Beacon/Pulsar en
conway_correctness. Los patrones infinitos de referencia requieren margen para
no confundir salida del tablero finito con una regla incorrecta.

## UBSan localizado

Ejemplo que no modifica el repositorio ni reutiliza flags Release como evidencia:

```bash
g++-14 -std=c++20 -O1 -g -fopenmp -fsanitize=undefined \
  -fno-sanitize-recover=all -Iinclude tests/input_tests.cpp \
  src/input/input.cpp src/core/game.cpp src/core/bs23.cpp \
  src/core/engines/sequential.cpp src/core/engines/openmp.cpp \
  -o /tmp/conway-input-ubsan
/tmp/conway-input-ubsan "$PWD"
/tmp/conway-input-ubsan "$PWD" --dimension-regressions
```

Para traza o controlador didáctico usar respectivamente tests/step_trace_tests.cpp
o tests/didactic_session_tests.cpp; el segundo requiere además
src/app/didactic_session.cpp y `-pthread`. No se comparan tiempos sanitizados con
Release. UBSan no demuestra ausencia de carreras ni sustituye las pruebas funcionales.

## Demos, scripts y herramientas

`conway_view_demo` usa Game real; `conway_simulation_demo` usa datos mock.
`dump_board`/`compare_boards.py` ayudan a comparar estados; `dump_board` acepta
`--engine seq|openmp` y `--require-threads N` (hilos por `OMP_NUM_THREADS`). Al
compilar Game a mano hay que incluir ambos motores y `-fopenmp`, aunque se
seleccione Secuencial.

Los scripts de tests usan compilaciones temporales. Las herramientas
scripts/measure_openmp.sh, scripts/measure_trace_overhead.sh y
scripts/test_navegacion.sh pueden generar evidencia: revisar sus destinos antes
de ejecutarlas. No son una campaña experimental oficial.

Los tests funcionales/PTY verifican pausa sin repintado periódico, restauración,
controles, navegación y resultados. La revisión visual Windows Terminal sigue
siendo complementaria; no se infiere de un test PTY la apariencia de cada fuente.

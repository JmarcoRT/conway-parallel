**Título:** feat(openmp): prototipo OpenMP aislado de una generación (S6)

## Cambios principales

- Nuevo motor `core::step_openmp(GameState&)` (`include/core/engines/openmp.hpp`, `src/core/engines/openmp.cpp`). Paraleliza por filas el cálculo de `next` con `#pragma omp parallel for schedule(static) reduction(+: nextAlive)`.
- Reutiliza `GameState`, `next_cell_state` (B3/S23) y `swap_buffers`; misma semántica que `step_sequential`. No duplica la regla ni crea otra representación del tablero.
- Prueba `tests/openmp_vs_sequential.cpp` + script `tests/run_openmp_prototype.sh` + target CMake `openmp_vs_sequential`.
- Documentación en `docs/OPENMP_PROTOTYPE.md` (reparto de trabajo, datos compartidos/privados, cómo ejecutar, resultado esperado, evidencia y fuentes).

## Pruebas realizadas

- `bash tests/run_openmp_prototype.sh` → `Resultado: 29 PASS, 0 FAIL`. Compara OpenMP vs secuencial generación a generación (área lógica, `generation`, `aliveCount`) con 1, 2, 4 y 8 hilos en block, blinker, glider, dos tableros aleatorios con semilla fija, y tableros de 1 fila / 1 columna.
- Blinker vuelve a su estado tras 2 generaciones con el motor OpenMP.
- `tests/run_input_tests.sh` sigue pasando.
- Compilado con `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` sin advertencias.
- Limitación: ejecutado en una máquina de 1 núcleo; falta repetirlo en multinúcleo.

## Impacto

- No se modificó ninguna interfaz compartida ni archivo existente salvo `CMakeLists.txt` (solo se añade un target dentro de `BUILD_TESTING`).
- `Game`, `main` y el ejecutable `conway` no cambian: OpenMP no está integrado todavía (S7).
- `core` base no depende de OpenMP; solo `openmp.cpp` y la prueba lo usan.

## Decisiones técnicas

- Paralelismo por filas con reparto estático: regiones de `next` disjuntas, sin `critical`/`atomic`; `aliveCount` por `reduction`.
- El número de hilos no se fija en el motor (`OMP_NUM_THREADS` / `omp_set_num_threads`).
- Sin benchmarks ni integración en esta semana.

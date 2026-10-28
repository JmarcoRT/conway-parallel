#pragma once

#include "core/game_state.hpp"
#include "core/cell_probe.hpp"
#include "core/step_trace.hpp"

namespace conway::core {

// Recorrido secuencial del Juego de la Vida.
// Mantiene su propio aliveCount dentro del bucle.
// Llama a next_cell_state(...) en cada (r, c) logica.
// Al terminar: fija aliveCount, aplica swap_buffers(state) e incrementa
// generation. Esta funcion es una de las implementaciones posibles del
// "motor" de la simulacion; OpenMP, Pthreads y MPI aportaran las suyas,
// todas reutilizando la misma funcion de regla (bs23::next_cell_state).
//
// Si sink != kNoTrace, construye una StepTrace observada y la entrega al
// sink al final del step. La rama de instrumentacion no introduce
// dependencias de OpenMP en este motor: usa std::chrono::steady_clock
// para medir el tiempo total del step y la duracion del bucle.
void step_sequential(GameState& s, StepTraceSink* sink = kNoTrace, CellProbe* cell = nullptr);

}  // namespace conway::core

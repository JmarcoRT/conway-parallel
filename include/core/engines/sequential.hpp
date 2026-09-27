#pragma once

#include "core/game_state.hpp"

namespace conway::core {

// Recorrido secuencial del Juego de la Vida.
// Mantiene su propio aliveCount dentro del bucle.
// Llama a next_cell_state(...) en cada (r, c) logica.
// Al terminar: fija aliveCount, aplica swap_buffers(state) e incrementa
// generation. Esta funcion es una de las implementaciones posibles del
// "motor" de la simulacion; OpenMP, Pthreads y MPI aportaran las suyas,
// todas reutilizando la misma funcion de regla (bs23::next_cell_state).
void step_sequential(GameState& s);

} // namespace conway::core

#pragma once

#include "core/game_state.hpp"

namespace conway::core {

// Prototipo OpenMP (S6): calcula UNA generacion sobre la misma GameState
// que usa el motor secuencial. Misma semantica que step_sequential:
//   - lee solo de `current`, escribe solo en `next`;
//   - fija aliveCount, hace swap_buffers(s) e incrementa generation.
// La regla B3/S23 NO se duplica: se invoca next_cell_state (core/bs23.hpp).
// El numero de hilos lo controla el entorno (OMP_NUM_THREADS) o
// omp_set_num_threads() desde quien llama; este motor no lo fija.
void step_openmp(GameState& s);

} // namespace conway::core

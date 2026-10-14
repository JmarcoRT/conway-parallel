#pragma once

#include "core/game_state.hpp"
#include "core/step_trace.hpp"

namespace conway::core {

// Prototipo OpenMP (S6): calcula UNA generacion sobre la misma GameState
// que usa el motor secuencial. Misma semantica que step_sequential:
//   - lee solo de `current`, escribe solo en `next`;
//   - fija aliveCount, hace swap_buffers(s) e incrementa generation.
// La regla B3/S23 NO se duplica: se invoca next_cell_state (core/bs23.hpp).
// El numero de hilos lo controla el entorno (OMP_NUM_THREADS) o
// omp_set_num_threads() desde quien llama; este motor no lo fija.
//
// Si sink != kNoTrace, cada hilo del team paralelo observa su primer y
// último r dentro del bucle, y deposita el resultado en un slot
// preasignado de un vector indexado por omp_get_thread_num(). La
// consolidación y la llamada al sink ocurren una sola vez, en el hilo
// original, tras la barrera implícita. Sin escrituras concurrentes a un
// mismo slot, sin secciones críticas, sin thread_local.
void step_openmp(GameState& s, StepTraceSink* sink = kNoTrace);

// Número de hilos observado en la región paralela del último step_openmp
// ejecutado por el hilo que consulta esta función.
int last_openmp_team_size() noexcept;

}  // namespace conway::core

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway::core {

// Representacion comun del estado del Juego de la Vida.
//
// Misma forma que usara cada motor:
//   - secuencial: height = alto logico completo del tablero.
//   - OpenMP / Pthreads: identico; todos los hilos ven la misma instancia.
//   - MPI: cada rank construye su propia instancia local (height = alto_local,
//     buffers dimensionados con halo 1 fila arriba y 1 fila abajo).
//
// El estado NO depende de OpenMP / Pthreads / MPI ni de la regla B3/S23.
// La regla vive en `next_cell_state` (include/core/bs23.hpp).
struct GameState {
    std::size_t width;
    std::size_t height;     // alto logico (o alto local en MPI)
    std::size_t stride;     // width + 2 (en MPI respeta la misma formula)
    std::vector<std::uint8_t> current;   // stride * (height + 2)
    std::vector<std::uint8_t> next;      // identico tamano
    std::size_t generation{0};
    std::size_t aliveCount{0};
};

// Intercambia los punteros logicos de los buffers (doble buffer).
// NO modifica generation ni aliveCount. Es invariante del modelo;
// cada motor lo invoca exactamente una vez al final de su paso.
inline void swap_buffers(GameState& s) {
    s.current.swap(s.next);
}

} // namespace conway::core

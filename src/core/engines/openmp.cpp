#include "core/engines/openmp.hpp"

#include "core/bs23.hpp"
#include "core/game_state.hpp"

#include <cstddef>
#include <cstdint>

namespace conway::core {

void step_openmp(GameState& s) {
    // Punteros y dimensiones en variables locales const: todos los hilos
    // las comparten en modo solo lectura (shared).
    const std::uint8_t* const pIn  = s.current.data();
    std::uint8_t* const       pOut = s.next.data();
    const std::size_t stride = s.stride;
    const std::size_t width  = s.width;
    const std::ptrdiff_t height = static_cast<std::ptrdiff_t>(s.height);

    std::size_t nextAlive = 0;

    // Reparto: las filas logicas 1..height se dividen en bloques contiguos
    // (schedule(static)). Cada hilo recorre sus filas completas, asi que
    // escribe en una region de `next` que ningun otro hilo toca: no hay
    // condiciones de carrera y no hacen falta secciones criticas.
    // `r`, `row` y `c` son privadas por estar declaradas dentro del bucle.
    // aliveCount se acumula con reduction(+): cada hilo lleva una copia
    // privada y OpenMP las suma al cerrar la region paralela.
    #pragma omp parallel for schedule(static) reduction(+ : nextAlive)
    for (std::ptrdiff_t r = 1; r <= height; ++r) {
        const std::size_t row = static_cast<std::size_t>(r);
        for (std::size_t c = 1; c <= width; ++c) {
            const std::uint8_t cell = next_cell_state(pIn, stride, row, c);
            pOut[row * stride + c] = cell;
            nextAlive += cell;
        }
    }
    // Barrera implicita al final de "parallel for": todos los hilos terminaron
    // de escribir `next` antes del swap (requisito de ARCHITECTURE.md).

    s.aliveCount = nextAlive;
    swap_buffers(s);
    ++s.generation;
}

} // namespace conway::core

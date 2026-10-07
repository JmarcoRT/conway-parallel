#include "core/engines/sequential.hpp"

#include "core/bs23.hpp"
#include "core/game_state.hpp"

namespace conway::core {

void step_sequential(GameState& s) {
    const std::uint8_t* const pIn  = s.current.data();
    std::uint8_t* const       pOut = s.next.data();

    std::size_t nextAlive = 0;
    for (std::size_t r = 1; r <= s.height; ++r) {
        for (std::size_t c = 1; c <= s.width; ++c) {
            const std::uint8_t cell =
                next_cell_state(pIn, s.stride, r, c);
            pOut[r * s.stride + c] = cell;
            nextAlive += cell;
        }
    }

    s.aliveCount = nextAlive;
    swap_buffers(s);
    ++s.generation;
}

} // namespace conway::core

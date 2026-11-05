#pragma once
#include "core/game.hpp"

namespace conway::detail {
// Read-only test view; no extra production accessor or conditional class layout.
struct GameTestAccess {
    static const core::GameState& state(const Game& game) noexcept { return game.state_; }
};
inline bool sameGameBuffers(const Game& a, const Game& b) {
    const auto& x = GameTestAccess::state(a);
    const auto& y = GameTestAccess::state(b);
    return x.width == y.width && x.height == y.height && x.stride == y.stride &&
           x.current == y.current && x.next == y.next &&
           x.generation == y.generation && x.aliveCount == y.aliveCount;
}
} // namespace conway::detail

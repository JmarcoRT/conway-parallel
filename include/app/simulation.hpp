#pragma once

#include "core/game.hpp"

namespace conway::app {

class SimulationSession {
public:
    SimulationSession(const InputData& input, Engine engine);

    const Game& game() const noexcept;
    bool isRunning() const noexcept;

    void step();
    void toggleRunning() noexcept;
    void advanceIfRunning();
    void reset();

private:
    InputData initialInput_;
    Engine engine_;
    Game game_;
    bool running_{false};
};

} // namespace conway::app

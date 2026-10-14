#include "app/simulation.hpp"

namespace conway::app {

SimulationSession::SimulationSession(const InputData& input, Engine engine)
    : initialInput_(input), engine_(engine), game_(input) {
    game_.setEngine(engine_);
}

const Game& SimulationSession::game() const noexcept {
    return game_;
}

bool SimulationSession::isRunning() const noexcept {
    return running_;
}

void SimulationSession::step() {
    game_.step();
}

void SimulationSession::toggleRunning() noexcept {
    running_ = !running_;
}

void SimulationSession::advanceIfRunning() {
    if (running_) {
        step();
    }
}

void SimulationSession::reset() {
    game_ = Game(initialInput_);
    game_.setEngine(engine_);
    running_ = false;
}

} // namespace conway::app

#include "core/game.hpp"

#include "core/engines/openmp.hpp"
#include "core/engines/sequential.hpp"

#include <algorithm>
#include <stdexcept>

namespace conway {

Game::Game(const InputData& input)
    : state_{} {
    state_.width = input.width;
    state_.height = input.height;
    state_.stride = input.width + 2;
    state_.current.assign(state_.stride * (state_.height + 2), 0);
    state_.next.assign(state_.stride * (state_.height + 2), 0);

    if (state_.width == 0 || state_.height == 0) {
        throw std::invalid_argument(
            "El tablero debe tener dimensiones mayores que cero"
        );
    }

    for (std::size_t row = 0; row < state_.height; ++row) {
        for (std::size_t col = 0; col < state_.width; ++col) {

            const std::size_t inputIndex =
                row * state_.width + col;

            const std::size_t gameIndex =
                (row + 1) * state_.stride + (col + 1);

            state_.current[gameIndex] =
                input.cells[inputIndex];

            if (state_.current[gameIndex] != 0) {
                ++state_.aliveCount;
            }
        }
    }
}

std::size_t Game::width() const noexcept {
    return state_.width;
}

std::size_t Game::height() const noexcept {
    return state_.height;
}

std::size_t Game::generation() const noexcept {
    return state_.generation;
}

std::size_t Game::aliveCount() const noexcept {
    return state_.aliveCount;
}

bool Game::isAlive(std::size_t row, std::size_t col) const {
    return state_.current.at(index(row, col)) != 0;
}

void Game::setCell(std::size_t row, std::size_t col, bool alive) {
    const std::size_t i = index(row, col);
    const bool wasAlive = state_.current.at(i) != 0;

    if (wasAlive == alive) {
        return;
    }

    state_.current[i] = alive ? 1 : 0;

    if (alive) {
        ++state_.aliveCount;
    } else {
        --state_.aliveCount;
    }
}

void Game::clear() {
    std::fill(state_.current.begin(), state_.current.end(), 0);
    std::fill(state_.next.begin(), state_.next.end(), 0);

    state_.generation = 0;
    state_.aliveCount = 0;
}

std::size_t Game::index(
    std::size_t row,
    std::size_t col
) const {

    if (row >= state_.height || col >= state_.width) {
        throw std::out_of_range(
            "Celda fuera de los limites del tablero"
        );
    }

    return (row + 1) * state_.stride + (col + 1);
}

void Game::step() {
    switch (engine_) {
        case Engine::Sequential:
            core::step_sequential(state_);
            return;
        case Engine::OpenMP:
            core::step_openmp(state_);
            return;
    }
    throw std::logic_error("Motor de ejecucion desconocido");
}

void Game::step(core::StepTraceSink* sink) {
    switch (engine_) {
        case Engine::Sequential:
            core::step_sequential(state_, sink);
            return;
        case Engine::OpenMP:
            core::step_openmp(state_, sink);
            return;
    }
    throw std::logic_error("Motor de ejecucion desconocido");
}

void Game::setEngine(Engine engine) noexcept {
    engine_ = engine;
}

Engine Game::engine() const noexcept {
    return engine_;
}

} // namespace conway

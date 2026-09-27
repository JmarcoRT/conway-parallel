#pragma once

#include "core/game_state.hpp"
#include "input/input.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway {

// Fachada que mantiene la API publica intacta y delega su representacion
// interna en core::GameState. Game NO depende de OpenMP / MPI / Pthreads.
// El calculo de la siguiente generacion se delega en core::step_sequential
// (motor secuencial); los futuros motores (OpenMP, Pthreads, MPI) daran
// implementaciones alternativas que operaran sobre la misma GameState.
class Game {
public:
    explicit Game(const InputData& input);

    std::size_t width() const noexcept;
    std::size_t height() const noexcept;
    std::size_t generation() const noexcept;
    std::size_t aliveCount() const noexcept;

    bool isAlive(std::size_t row, std::size_t col) const;

    void setCell(std::size_t row, std::size_t col, bool alive);
    void clear();

    void step();

private:
    core::GameState state_;

    std::size_t index(std::size_t row, std::size_t col) const;
};

} // namespace conway

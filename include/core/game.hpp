#pragma once

#include "core/game_state.hpp"
#include "input/input.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway {

// Motor que usa Game::step() para calcular la siguiente generacion.
// Todos operan sobre la misma core::GameState y producen el mismo resultado
// observable (mismas dimensiones, misma generacion, mismo aliveCount y mismas
// celdas), lo cual es verificado por tests/game_engine_tests.cpp.
enum class Engine {
    Sequential,  // core::step_sequential (referencia de correccion; por defecto)
    OpenMP       // core::step_openmp (numero de hilos: OMP_NUM_THREADS / omp_set_num_threads)
};

// Fachada que mantiene la API publica intacta y delega su representacion
// interna en core::GameState. Game NO incluye omp.h ni usa pragmas: solo
// elige a que motor (core::step_sequential o core::step_openmp) delega step().
// El motor por defecto es Engine::Sequential; los futuros motores (Pthreads,
// MPI) se anadiran como valores adicionales de Engine.
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

    // Selecciona el motor usado por step(). No modifica el tablero, la
    // generacion ni aliveCount: se puede cambiar entre generaciones.
    void setEngine(Engine engine) noexcept;
    Engine engine() const noexcept;

private:
    core::GameState state_;
    Engine engine_{Engine::Sequential};

    std::size_t index(std::size_t row, std::size_t col) const;
};

} // namespace conway

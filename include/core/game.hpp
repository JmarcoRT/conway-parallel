#pragma once

#include "core/game_state.hpp"
#include "core/cuda_launch.hpp"
#include "core/step_trace.hpp"
#include "input/input.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

namespace conway {
namespace detail { struct GameTestAccess; }

// Selección de motor. Game::step() solo admite los motores locales; MPI se
// coordina desde SimulationSession en un trabajador externo.
// Los motores locales operan sobre la misma core::GameState y producen el mismo resultado
// observable (mismas dimensiones, misma generacion, mismo aliveCount y mismas
// celdas), lo cual es verificado por tests/game_engine_tests.cpp.
enum class Engine {
    Sequential,  // core::step_sequential (referencia de correccion; por defecto)
    OpenMP,      // core::step_openmp (numero de hilos: OMP_NUM_THREADS / omp_set_num_threads)
    MPI,         // Owned by SimulationSession; never executed by a local Game.
    CUDA         // Optional local GPU engine; explicit errors, no CPU fallback.
};

// Fachada que mantiene la API publica intacta y delega su representacion
// interna en core::GameState. Game NO incluye omp.h ni usa pragmas: solo
// elige el motor local. CUDA mantiene una cache exclusiva y publica snapshots CPU.
// El motor por defecto es Engine::Sequential. La selección MPI pertenece a la
// sesión, nunca a un Game local que pueda copiarse para visualización.
class Game {
public:
    explicit Game(const InputData& input);
    // Copies own independent CPU buffers and start without a GPU cache. Moves
    // transfer exclusive ownership; moved-from objects can be assigned or destroyed.
    ~Game();
    Game(const Game&);
    Game& operator=(const Game&);
    Game(Game&&) noexcept;
    Game& operator=(Game&&) noexcept;

    // Variante que fija la generacion inicial a `start_generation`.
    // Util para construir un Game que representa el estado de la
    // generacion N sin tener que ejecutar N pasos desde gen 0. El
    // contenido de las celdas se toma de `input`; el contador de
    // generacion y el contador de celdas vivas se inicializan
    // coherentemente con `start_generation`.
    Game(const InputData& input, std::size_t start_generation);

    std::size_t width() const noexcept;
    std::size_t height() const noexcept;
    std::size_t generation() const noexcept;
    std::size_t aliveCount() const noexcept;

    bool isAlive(std::size_t row, std::size_t col) const;

    void setCell(std::size_t row, std::size_t col, bool alive);
    void clear();

    void step();

    // Sobrecarga opcional: si sink != conway::core::kNoTrace, el motor
    // delegado construye una StepTrace observada y la entrega al sink al
    // final del step. Sin sink, el comportamiento es idéntico a step().
    // CUDA rechaza un sink no nulo antes de avanzar o modificar estado.
    void step(core::StepTraceSink* sink);

    // Selecciona el motor usado por step(). No modifica el tablero, la
    // generacion ni aliveCount: se puede cambiar entre generaciones.
    void setEngine(Engine engine) noexcept;
    Engine engine() const noexcept;

    core::CudaBlockSize cudaBlockSize() const noexcept;
    // Requires compiled CUDA and a visible device, even before the first step.
    // Failure preserves configuration, cache and confirmed CPU state.
    void setCudaBlockSize(core::CudaBlockSize block);
    core::CudaLaunchGeometry cudaLaunchConfiguration() const;

private:
    // Test-only definition lives in tests/game_test_access.hpp, never in production.
    friend struct detail::GameTestAccess;
    core::CudaBlockSize cudaBlock_{core::kDefaultCudaBlock};
    // Stable layout in CUDA ON/OFF builds. Never shared by copies.
    struct GpuCache;
    std::unique_ptr<GpuCache> gpu_;
    core::GameState state_;
    void swap(Game&) noexcept;
    void stepCuda(core::StepTraceSink*);
    Engine engine_{Engine::Sequential};

    std::size_t index(std::size_t row, std::size_t col) const;
};

} // namespace conway

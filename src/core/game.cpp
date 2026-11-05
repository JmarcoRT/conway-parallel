#include "core/game.hpp"
#include "input/validated_dimensions.hpp"

#include "core/engines/openmp.hpp"
#include "core/engines/sequential.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <type_traits>
#ifdef CONWAY_HAS_CUDA
#include "core/engines/cuda.hpp"
#endif

namespace conway {

struct Game::GpuCache {
#ifdef CONWAY_HAS_CUDA
    core::CudaBackend backend;
    GpuCache(const InputData& input, std::size_t generation, core::CudaBlockSize block)
        : backend(input, generation, 0, block) {}
#endif
};

Game::~Game() = default;
Game::Game(const Game& other)
    : cudaBlock_(other.cudaBlock_), state_(other.state_), engine_(other.engine_) {}
Game& Game::operator=(const Game& other) {
    if (this != &other) { Game candidate(other); swap(candidate); }
    return *this;
}
Game::Game(Game&& other) noexcept : state_{} { swap(other); }
Game& Game::operator=(Game&& other) noexcept {
    if (this != &other) { Game candidate(std::move(other)); swap(candidate); }
    return *this;
}
void Game::swap(Game& other) noexcept {
    static_assert(std::is_nothrow_swappable_v<core::GameState>);
    using std::swap;
    swap(cudaBlock_, other.cudaBlock_);
    swap(state_, other.state_);
    swap(engine_, other.engine_);
    swap(gpu_, other.gpu_);
}

Game::Game(const InputData& input)
    : state_{} {
    const auto shape = conway::input::detail::validateDimensions(input.width, input.height);
    if (input.cells.size() != shape.cells)
        throw std::invalid_argument("La cantidad de celulas no coincide con las dimensiones.");
    if (std::any_of(input.cells.begin(), input.cells.end(), [](auto cell) { return cell > 1; }))
        throw std::invalid_argument("El tablero solo puede contener 0 y 1.");
    state_.width = input.width;
    state_.height = input.height;
    state_.stride = shape.stride;
    state_.current.assign(shape.padded, 0);
    state_.next.assign(shape.padded, 0);

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

Game::Game(const InputData& input, std::size_t start_generation)
    : Game(input) {
    // El constructor base asigna state_.generation = 0; forzamos
    // el valor de partida. aliveCount y celdas se conservan.
    state_.generation = start_generation;
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

    gpu_.reset();
    state_.current[i] = alive ? 1 : 0;

    if (alive) {
        ++state_.aliveCount;
    } else {
        --state_.aliveCount;
    }
}

void Game::clear() {
    gpu_.reset();
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

void Game::step() { step(core::kNoTrace); }

void Game::stepCuda(core::StepTraceSink* sink) {
    if (sink) throw std::logic_error("CUDA step tracing is not supported");
#ifdef CONWAY_HAS_CUDA
    try {
        if (!gpu_) {
            InputData input{state_.width, state_.height,
                            std::vector<std::uint8_t>(state_.width * state_.height)};
            for (std::size_t r = 0; r < state_.height; ++r)
                for (std::size_t c = 0; c < state_.width; ++c)
                    input.cells[r * state_.width + c] = state_.current[(r + 1) * state_.stride + c + 1];
            gpu_ = std::make_unique<GpuCache>(input, state_.generation, cudaBlock_);
        }
        gpu_->backend.step();
        auto candidate = gpu_->backend.snapshot();
        static_assert(std::is_nothrow_move_assignable_v<core::GameState>);
        state_ = std::move(candidate); // Publish both buffers and counters together.
    } catch (...) {
        // GPU may be ahead, even when a CPU allocation failed in snapshot().
        gpu_.reset();
        throw;
    }
#else
    throw std::logic_error("CUDA backend was not compiled (CONWAY_ENABLE_CUDA=OFF)");
#endif
}

void Game::step(core::StepTraceSink* sink) {
    switch (engine_) {
        case Engine::CUDA:
            stepCuda(sink);
            return;
        case Engine::Sequential:
            gpu_.reset();
            core::step_sequential(state_, sink);
            return;
        case Engine::OpenMP:
            gpu_.reset();
            core::step_openmp(state_, sink);
            return;
        case Engine::MPI:
            throw std::logic_error("MPI requiere una sesion distribuida, no Game::step()");
    }
    throw std::logic_error("Motor de ejecucion desconocido");
}

void Game::setEngine(Engine engine) noexcept {
    if (engine_ != engine) gpu_.reset();
    engine_ = engine;
}

core::CudaBlockSize Game::cudaBlockSize() const noexcept { return cudaBlock_; }

void Game::setCudaBlockSize(core::CudaBlockSize block) {
    core::validateCudaBlock(block);
#ifdef CONWAY_HAS_CUDA
    if (gpu_) gpu_->backend.setBlockSize(block);
    else (void)core::CudaBackend::launchConfiguration(state_.width, state_.height, block);
    cudaBlock_ = block;
#else
    throw std::logic_error("CUDA backend was not compiled (CONWAY_ENABLE_CUDA=OFF)");
#endif
}

core::CudaLaunchGeometry Game::cudaLaunchConfiguration() const {
#ifdef CONWAY_HAS_CUDA
    if (gpu_) return gpu_->backend.launchConfiguration();
    return core::CudaBackend::launchConfiguration(state_.width, state_.height, cudaBlock_);
#else
    throw std::logic_error("CUDA backend was not compiled (CONWAY_ENABLE_CUDA=OFF)");
#endif
}

Engine Game::engine() const noexcept {
    return engine_;
}

} // namespace conway

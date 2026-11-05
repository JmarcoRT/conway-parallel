#pragma once
#include "core/game.hpp"
#include "mpi/client.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace conway::app {
mpi::ClientOptions defaultMPIOptions(int processes=1);
int recommendedMPIProcesses(std::size_t height=1024) noexcept;
std::optional<int> parseMPIProcesses(std::string_view text) noexcept;

// UI-thread owner. Only the private worker touches mpi::Client; deliveries are
// committed by poll(). game() always returns the last confirmed full snapshot.
class SimulationSession {
public:
    // backgroundLocal reuses the worker for a sequential catalogue only; no speculative NEXT.
    // Default local/MPI behavior remains compatible with existing consumers.
    SimulationSession(const InputData& input, Engine engine,
                      mpi::ClientOptions options=defaultMPIOptions(),
                      std::function<void()> notify={}, bool backgroundLocal=false,
                      core::CudaBlockSize block=core::kDefaultCudaBlock);
    ~SimulationSession();
    const Game& game() const noexcept;
    const std::optional<Game>& preview() const noexcept { return preview_; }
    bool isRunning() const noexcept;
    bool busy() const noexcept { return busy_; }
    bool closed() const noexcept { return closed_; }
    Engine engine() const noexcept { return engine_; }
    int processes() const noexcept { return options_.processes; }
    const std::string& error() const noexcept { return error_; }
    const std::vector<int>& mpiWorkerPids() const noexcept { return pids_; }
    void poll();
    void step();
    void toggleRunning() noexcept;
    void pause() noexcept { running_=false; }
    void advanceIfRunning();
    void reset();
    void configure(Engine engine,int processes,
                   core::CudaBlockSize block=core::kDefaultCudaBlock);
    void recover();
    void close();
private:
    enum class Action { Configure, Step, Reset, Close };
    struct Worker;
    void submit(Action action);
    void updatePreview();
    InputData initialInput_;
    Engine engine_;
    core::CudaBlockSize cudaBlock_{core::kDefaultCudaBlock};
    Game game_;
    std::optional<Game> preview_;
    mpi::ClientOptions options_;
    std::function<void()> notify_;
    std::unique_ptr<Worker> worker_;
    std::optional<Action> pending_;
    bool backgroundLocal_{false};
    std::uint64_t epoch_{0};
    bool running_{false},busy_{false},closed_{false};
    std::string error_;
    std::vector<int> pids_;
};
} // namespace conway::app

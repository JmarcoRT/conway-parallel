#pragma once
#include "core/game.hpp"
#include "core/step_trace.hpp"
#include "mpi/client.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace conway::app {
enum class DidacticPhase { Idle, Preparing, Ready, Closing, Closed, Error };
// Presentation stages, NOT a global execution timeline. Local traces only
// support recorded work/coverage and the resulting generation, never halo/cell events.
enum class DidacticStage { LocalWork, GenerationResult, MPIState, MPIExchangeFirst,
    MPIExchangeSecond, MPICell, MPIBuffers };
struct DidacticCapture {
    Game before, after;
    std::optional<core::StepTrace> localTrace;
    std::shared_ptr<const mpi::MpiStepTrace> mpiTrace;
};
struct DidacticRequest {
    std::uint64_t epoch{};
    Engine engine{Engine::Sequential};
    int threads{1};
    mpi::ClientOptions mpiOptions;
    std::shared_ptr<const InputData> initial;
    mpi::TraceRequest selection; // expected generation + global selected cell.
    bool reset{false};
};
// Owned exclusively by the capture worker. Injection enables gated deterministic
// tests without putting timing, pauses or test hooks into the actual engine.
class DidacticBackend {
public:
    virtual ~DidacticBackend()=default;
    virtual std::shared_ptr<const DidacticCapture> capture(const DidacticRequest&)=0;
    virtual void close()=0;
    virtual bool ownsMPI() const noexcept=0;
    virtual bool cleanupConfirmed() const noexcept=0;
};
std::unique_ptr<DidacticBackend> makeDidacticBackend();
struct DidacticWorkerView {
    int thread_id=-1,first_row=0,end_row=0;
    std::size_t cells_processed=0;
    std::uint64_t loop_duration_ns=0;
    double progress=0;
    int color_index=0;
    bool is_sequential=false;
};
struct DidacticFrame {
    bool has_data=false,is_preparing=false,playing=false,fully_revealed=false;
    bool closing=false,closed=false,owns_mpi=false,cleanup_confirmed=true;
    DidacticPhase phase=DidacticPhase::Idle;
    std::string engine_name;
    int requested_threads=0,effective_threads=0,logical_processors=0;
    int requested_processes=0,observed_processes=0;
    std::size_t current_generation=0,next_generation=0,viewed_generation=0;
    std::optional<std::size_t> captured_generation; // Latest accepted computation, distinct from viewed generation.
    std::size_t width=0,height=0,current_alive=0,next_alive=0;
    std::vector<std::uint8_t> revealed,next_alive_map,current_alive_map;
    std::vector<std::int8_t> worker_id;
    std::vector<DidacticWorkerView> workers; // LOCAL observed threads only; MPI ranks remain in mpi_trace.
    std::shared_ptr<const mpi::MpiStepTrace> mpi_trace;
    std::vector<DidacticStage> stages;
    std::size_t stage_index=0;
    std::uint64_t stage_duration_ns=2000000000ULL,stage_elapsed_ns=0;
    std::uint64_t total_playback_ns=0,current_playback_ns=0;
    double speed=1;
    std::string error_message;
};
// UI-thread owner. A persistent worker owns the backend/MPI session. poll()
// accepts immutable deliveries; no worker ever changes playback state.
// Reset/close supersede requests by epoch and coalesce while one command is in
// flight. The destructor joins only as a final shutdown fallback; UI paths must
// use close()/poll() and retain this object until closed().
class DidacticController {
public:
    using Phase=DidacticPhase;
    // notify executes on the worker: post an event only, never touch UI state.
    explicit DidacticController(std::function<void()> notify={},std::unique_ptr<DidacticBackend> backend={});
    ~DidacticController();
    DidacticController(const DidacticController&)=delete;
    DidacticController& operator=(const DidacticController&)=delete;
    void setInitialInput(const InputData&);
    bool hasInitialInput() const {return bool(initial_);}
    const InputData& initialInput() const;
    void startPreparation(const InputData&,Engine,int threads,mpi::ClientOptions options={},
                          std::size_t selectedRow=0,std::size_t selectedColumn=0);
    void prepareSynchronously(const InputData&,Engine,int threads); // Blocking helper, never used by UI.
    void waitUntilReady(); // Blocking test/shutdown helper; polls deliveries, never advances playback.
    void joinWorker(); // Compatibility wait helper, not a UI operation.
    void poll();
    Phase phase() const {return phase_;}
    bool isReady() const {return phase_==Phase::Ready;}
    bool isPreparing() const {return phase_==Phase::Preparing;}
    bool busy() const {return busy_;}
    bool closed() const {return phase_==Phase::Closed;}
    bool ownsMPISession() const {return mpiActive_;} // Includes queued creation, close pending and uncertain cleanup.
    bool cleanupConfirmed() const {return cleanupConfirmed_;}
    const std::string& errorMessage() const {return error_;}
    void play();
    void pause();
    void togglePlay();
    bool isPlaying() const {return playing_;}
    void setSpeed(double);
    void increaseSpeed();
    void decreaseSpeed();
    double speed() const {return speed_;}
    void setStageDuration(std::chrono::nanoseconds); // Positive, fixed presentation duration, independent of capture timing.
    void tick(std::chrono::nanoseconds);
    void stepStage(); // Exactly one stage (or next capture at the end), only while paused.
    void advanceGeneration(); // Explicit compatibility action, only paused; UI uses stepStage instead.
    void resetToInitial();
    void close(); // Nonblocking; invalidates deliveries immediately, cleanup stays on worker.
    DidacticFrame snapshot() const;
    const Game* preStep() const;
    const Game* postStep() const;
    const core::StepTrace* stepTrace() const;
    std::shared_ptr<const DidacticCapture> capture() const {return capture_;}
private:
    struct Worker;
    struct Job {DidacticRequest request;bool close=false;};
    void enqueue(Job);
    void submit(Job);
    void nextCapture();
    void nextStage();
    std::vector<DidacticStage> stages() const;
    std::unique_ptr<Worker> worker_;
    std::shared_ptr<const InputData> initial_;
    std::shared_ptr<const DidacticCapture> capture_;
    DidacticRequest configuration_;
    std::optional<Job> pending_;
    std::uint64_t epoch_=0;
    Phase phase_=Phase::Idle;
    bool busy_=false,playing_=false,mpiActive_=false,cleanupConfirmed_=true;
    std::string error_;
    std::size_t stage_=0;
    std::uint64_t stageElapsed_=0,stageDuration_=2000000000ULL;
    double speed_=1;
};
}

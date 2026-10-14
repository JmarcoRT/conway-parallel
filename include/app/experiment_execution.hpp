#pragma once
#include "app/experiment_form.hpp"
#include "app/experiment_metadata.hpp"
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>

namespace conway::app {
enum class ExecutionPhase { Ready, Preparing, Running, Finished, Error };
struct ExecutionDescription {
    std::uint64_t id{};
    bool scaling{};
    ExperimentFormConfig config;
    std::string patternName;
    std::size_t width{}, height{};
};
struct ExecutionRequest {
    ExecutionDescription description;
    std::optional<InputData> loaded; // Owned snapshot; generated inputs are prepared by the worker.
};
struct StoredExperiment {
    ExecutionDescription description;
    ExperimentContext context;
    std::variant<ExperimentResult, ScalingResult> result;
};
struct ExecutionSnapshot {
    ExecutionPhase phase{ExecutionPhase::Ready};
    ExecutionDescription description;
    std::string error;
    std::shared_ptr<const StoredExperiment> latest; // Last successful delivery survives failures.
    bool active() const { return phase == ExecutionPhase::Preparing || phase == ExecutionPhase::Running; }
};
class ExperimentExecution {
public:
    using PhasePublisher = std::function<void(ExecutionPhase)>;
    using Backend = std::function<StoredExperiment(const ExecutionRequest&, const PhasePublisher&)>;
    explicit ExperimentExecution(std::function<void()> notify, Backend backend = {});
    ~ExperimentExecution();
    ExperimentExecution(const ExperimentExecution&) = delete;
    ExperimentExecution& operator=(const ExperimentExecution&) = delete;
    // UI-thread caller. Preflight counts retained UI payload and previous results
    // against the existing 256 MiB policy, then copies a loaded input once.
    bool start(bool scaling, ExperimentFormConfig config, const InputData* loaded,
               std::string name, std::size_t retainedUiBytes = 0);
    ExecutionSnapshot snapshot() const;
    // Public for deterministic stale-event tests; mismatched run IDs are ignored.
    bool publishPhase(std::uint64_t id, ExecutionPhase phase);
    void shutdown(); // Joins only on shutdown, never while navigating/polling/launching.
private:
    void loop();
    std::function<void()> notify_;
    Backend backend_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    ExecutionSnapshot state_;
    std::optional<ExecutionRequest> pending_;
    bool stopping_{false};
    std::thread worker_;
};
} // namespace conway::app

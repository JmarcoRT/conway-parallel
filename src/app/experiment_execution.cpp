#include "app/experiment_execution.hpp"
#include "experiment_input_detail.hpp"
#include <limits>
#include <stdexcept>

namespace conway::app {
namespace {
using detail::checkedAdd;
using detail::checkedMultiply;
constexpr std::size_t budget = input::GenerationLimits{}.max_memory_bytes;
std::size_t retained(const StoredExperiment& stored) {
    auto bytes = std::visit([](const auto& result) {
        auto value = checkedMultiply(result.samples.capacity(), sizeof(typename std::decay_t<decltype(result.samples)>::value_type));
        if constexpr (std::is_same_v<std::decay_t<decltype(result)>, ScalingResult>) {
            value = checkedAdd(value, checkedMultiply(result.summary.points.capacity(), sizeof(ScalingPoint)));
            value = checkedAdd(value, checkedMultiply(result.config.requestedThreads.capacity(), sizeof(std::int64_t)));
        }
        if(result.study) {
            const auto& study=*result.study;
            value=checkedAdd(value,checkedMultiply(study.samples.capacity(),sizeof(StudySample)));
            value=checkedAdd(value,checkedMultiply(study.sessions.capacity(),sizeof(StudySession)));
            value=checkedAdd(value,checkedMultiply(study.configurations.capacity(),sizeof(ExecutionConfiguration)));
            value=checkedAdd(value,checkedMultiply(study.summaries.capacity(),sizeof(StudySummary)));
            for(const auto& session:study.sessions)
                value=checkedAdd(value,checkedAdd(session.runtime.capacity()+session.diagnostic.capacity(),checkedMultiply(session.workerPids.capacity(),sizeof(int))));
        }
        return value;
    }, stored.result);
    for (const auto& field : stored.context.fields)
        bytes = checkedAdd(bytes, sizeof(MetadataField) + field.key.capacity() + field.detail.capacity()
                                  + (field.value ? field.value->capacity() : 0));
    return checkedAdd(bytes, sizeof(StoredExperiment));
}
void preflight(const ExecutionDescription& d, std::size_t uiBytes, const StoredExperiment* previous) {
    const auto padded = checkedMultiply(checkedAdd(d.width, 2), checkedAdd(d.height, 2));
    auto bytes = checkedAdd(uiBytes, checkedMultiply(d.width, d.height)); // Worker input, in addition to UI.
    bytes = checkedAdd(bytes, checkedMultiply(4, padded)); // Oracle and current Game.
    if (previous) bytes = checkedAdd(bytes, retained(*previous));
    const auto repetitions = static_cast<std::size_t>(d.scaling ? d.config.scaling.repetitions : d.config.comparison.repetitions);
    const bool generalized=d.scaling?d.config.scaling.parallelEngine==Engine::MPI:!d.config.comparison.participants.empty();
    const auto threads = d.scaling ? (d.config.scaling.parallelEngine==Engine::MPI?
        d.config.scaling.requestedProcesses.size():d.config.scaling.requestedThreads.size()) :
        (generalized?d.config.comparison.participants.size()-1:1);
    const auto samples = checkedMultiply(checkedAdd(threads, 1), repetitions);
    if(generalized) {
        bytes=checkedAdd(bytes,checkedMultiply(samples,sizeof(StudySample)+sizeof(StudySession)+8192+1024*sizeof(int)));
    }
    // Result records plus summarization workspace and retained configuration copies.
    bytes = checkedAdd(bytes, checkedMultiply(samples, d.scaling ? sizeof(ScalingSample) : sizeof(ExperimentSample)));
    bytes = checkedAdd(bytes, checkedMultiply(checkedMultiply(2, repetitions), sizeof(ExperimentSample)));
    bytes = checkedAdd(bytes, checkedMultiply(checkedMultiply(3, repetitions), sizeof(double)));
    bytes = checkedAdd(bytes, checkedMultiply(threads, sizeof(ScalingPoint) + 4 * sizeof(std::int64_t)));
    if (bytes > budget) throw std::length_error("Entrada, snapshots y resultados superan el presupuesto de 256 MiB.");
}
StoredExperiment execute(const ExecutionRequest& request, const ExperimentExecution::PhasePublisher& phase) {
    const auto& d = request.description;
    MetadataOptions options;
    options.studyId = "ftxui-" + std::to_string(d.id);
    if (!d.config.generated) options.patternName = d.patternName;
    std::optional<input::GeneratedInput> generated;
    if (d.config.generated) generated = input::generateInput(*d.config.generated); // Exactly once, before context/timing.
    auto context = captureExperimentContext(options); // In the same worker, immediately before the executor.
    phase(ExecutionPhase::Running);
    if (d.scaling) {
        auto result = generated ? runScalingStudy(*generated, d.config.scaling)
                                : runScalingStudy(*request.loaded, d.config.scaling);
        return {d, std::move(context), std::move(result)};
    }
    auto result = generated ? runExperiment(*generated, d.config.comparison)
                            : runExperiment(*request.loaded, d.config.comparison);
    return {d, std::move(context), std::move(result)};
}
} // namespace
ExperimentExecution::ExperimentExecution(std::function<void()> notify, Backend backend)
    : notify_(std::move(notify)), backend_(backend ? std::move(backend) : execute),
      worker_([this] { loop(); }) {}
ExperimentExecution::~ExperimentExecution() { shutdown(); }
ExecutionSnapshot ExperimentExecution::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}
bool ExperimentExecution::start(bool scaling, ExperimentFormConfig config, const InputData* loaded,
                                std::string name, std::size_t uiBytes) {
    std::lock_guard lock(mutex_);
    if (state_.active() || stopping_) return false;
    if(state_.latest && std::visit([](const auto& result) {
            return result.study && !result.study->cleanupConfirmed;
        },state_.latest->result)) {
        state_.phase=ExecutionPhase::Error;
        state_.error="Previous MPI cleanup is unconfirmed; no new measurement was started.";
        return true;
    }
    ExecutionDescription description{state_.description.id + 1, scaling, std::move(config), std::move(name)};
    if (description.config.generated) {
        description.width = description.config.generated->width;
        description.height = description.config.generated->height;
    } else if (loaded) {
        description.width = loaded->width;
        description.height = loaded->height;
    }
    state_.description = description;
    state_.error.clear();
    try {
        if (!description.width || !description.height) throw std::invalid_argument("No hay una entrada seleccionada.");
        preflight(description, uiBytes, state_.latest.get());
        ExecutionRequest request{description, std::nullopt};
        if (!description.config.generated) request.loaded = *loaded;
        pending_ = std::move(request);
        state_.phase = ExecutionPhase::Preparing;
        wake_.notify_one();
    } catch (const std::exception& error) {
        state_.phase = ExecutionPhase::Error;
        state_.error = error.what();
    }
    return true;
}
bool ExperimentExecution::publishPhase(std::uint64_t id, ExecutionPhase phase) {
    std::lock_guard lock(mutex_);
    if (state_.description.id != id || !state_.active() || phase != ExecutionPhase::Running) return false;
    state_.phase = phase;
    return true;
}
void ExperimentExecution::loop() {
    for (;;) {
        ExecutionRequest request;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (!pending_) return;
            request = std::move(*pending_);
            pending_.reset();
        }
        const auto id = request.description.id;
        std::shared_ptr<const StoredExperiment> result;
        std::string error;
        try {
            result = std::make_shared<StoredExperiment>(backend_(request, [this, id](ExecutionPhase phase) {
                if (publishPhase(id, phase)) notify_();
            }));
            error=std::visit([](const auto& data) {
                if(!data.study) return std::string{};
                if(!data.study->diagnostic.empty()) return data.study->diagnostic;
                if(!data.study->complete || !data.study->cleanupConfirmed)
                    return std::string("Experimental study is incomplete or MPI cleanup is unconfirmed.");
                return std::string{};
            },result->result);
        } catch (const std::exception& failure) { error = failure.what(); }
        catch (...) { error = "Error desconocido durante el experimento."; }
        // Drop the board before publishing terminal state/accepting the next job.
        request.loaded.reset();
        {
            std::lock_guard lock(mutex_);
            if (state_.description.id == id) {
                if (result) {
                    state_.latest = std::move(result); state_.error=std::move(error);
                    state_.phase = state_.error.empty()?ExecutionPhase::Finished:ExecutionPhase::Error;
                }
                else { state_.error = std::move(error); state_.phase = ExecutionPhase::Error; }
            }
        }
        notify_();
    }
}
void ExperimentExecution::shutdown() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}
} // namespace conway::app

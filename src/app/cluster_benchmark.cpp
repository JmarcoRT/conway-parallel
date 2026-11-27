#include "app/cluster_benchmark.hpp"

#include "benchmark_detail.hpp"
#include "experiment_input_detail.hpp"

namespace conway::app {
namespace {

bool matches(const ClusterMeasurement& sample, const Game& oracle) {
    const auto& board = sample.finalBoard;
    if (sample.generation != oracle.generation() || board.width != oracle.width() || board.height != oracle.height())
        return false;
    for (std::size_t row = 0; row < board.height; ++row)
        for (std::size_t column = 0; column < board.width; ++column)
            if (board.cells[row * board.width + column] != oracle.isAlive(row, column)) return false;
    return true;
}

void checkGroup(ClusterSession& group) {
    const auto state = group.snapshot();
    if (state.phase != ClusterPhase::Ready)
        throw std::runtime_error("Grupo MPI perdido durante Benchmark. " + state.message);
}

} // namespace

StoredExperiment runClusterBenchmark(ClusterSession& group, const ExecutionRequest& request,
    const ExperimentExecution::PhasePublisher& phase, const std::function<bool()>& cancelled) {
    const auto& description = request.description;
    auto config = description.config.comparison;
    const auto groupState = group.snapshot();
    checkGroup(group);
    const int processes = static_cast<int>(groupState.members.size());
    const auto ensureActive = [&] {
        checkGroup(group);
        if (cancelled()) throw std::runtime_error("Benchmark cancelado después del último intento en curso.");
    };

    std::optional<input::GeneratedInput> generated;
    if (description.config.generated) generated = input::generateInput(*description.config.generated);
    const auto& board = generated ? generated->input : *request.loaded;
    auto identity = detail::validateInput(board);
    detail::attachGeneratedMetadata(identity, board, generated ? &generated->metadata : nullptr);
    auto context = captureExperimentContext({"cluster-" + std::to_string(description.id),
        generated ? std::nullopt : std::optional{description.patternName}, {}});
    config.participants = {{Engine::Sequential}, {Engine::MPI, {}, processes}};
    ExperimentalStudy study{};
    study.configurations = config.participants;
    study.generations = config.generations;
    study.warmups = config.warmups;
    study.repetitions = config.repetitions;
    study.rotateRight = config.rotateRight;
    study.width = board.width;
    study.height = board.height;
    study.maxMemoryBytes = config.maxMemoryBytes;
    const auto padded = detail::checkedMultiply(detail::checkedAdd(board.width, 2), detail::checkedAdd(board.height, 2));
    const auto attempts = detail::checkedMultiply(2, detail::checkedAdd(config.warmups, config.repetitions));
    study.estimatedPeakBytes = detail::checkedAdd(detail::checkedMultiply(4, padded),
        detail::checkedAdd(detail::checkedMultiply(2, board.cells.size()),
            detail::checkedMultiply(2u * config.repetitions, sizeof(StudySample))));
    if (study.estimatedPeakBytes > study.maxMemoryBytes)
        throw std::length_error("Benchmark del clúster supera el presupuesto de memoria.");
    for (const auto& member : groupState.members) study.distributedRanks.push_back({member.hostname, member.ip});

    ensureActive();
    phase(ExecutionPhase::Running);
    std::size_t completed = 0;
    request.progress(0, attempts);
    auto oracle = detail::runOnce(board, Engine::Sequential, 1, config.generations).finalGame;
    bool first = true;
    const auto measure = [&](std::size_t index) {
        ensureActive();
        StudySample sample{};
        sample.configurationIndex = index;
        sample.executionIndex = 1 + completed; // The independent oracle is execution zero.
        if (index == 0) {
            auto local = detail::runOnce(board, Engine::Sequential, 1, config.generations);
            sample.elapsedMs = local.elapsedMs;
            sample.minEffectiveThreads = sample.maxEffectiveThreads = 1;
            sample.equivalent = detail::sameState(local.finalGame, oracle);
        } else {
            const auto start = detail::Clock::now();
            auto initial = first ? std::make_shared<const InputData>(board) : nullptr;
            const auto id = group.submit(ClusterSession::BenchmarkAttempt{config.generations, std::move(initial)});
            if (!id) throw std::runtime_error("El grupo MPI dejó de estar disponible para Benchmark.");
            const auto result = group.waitForResult(*id);
            if (!result->measurement) throw std::runtime_error(result->error.empty() ?
                "Falta el resultado de Benchmark MPI." : result->error);
            first = false;
            const auto& mpi = *result->measurement;
            sample.requestMs = std::chrono::duration<double, std::milli>(detail::Clock::now() - start).count();
            sample.elapsedMs = mpi.elapsedMs;
            sample.loadMs = mpi.prepareMs;
            sample.snapshotMs = mpi.collectMs;
            sample.observedProcesses = mpi.processes;
            sample.equivalent = matches(mpi, oracle);
        }
        sample.valid = sample.equivalent && (index == 0 || sample.observedProcesses == processes);
        request.progress(++completed, attempts);
        return sample;
    };

    for (int warmup = 0; warmup < config.warmups; ++warmup) {
        for (std::size_t index = 0; index < 2; ++index) {
            if (!measure(index).valid) throw std::runtime_error("Calentamiento sin equivalencia con la referencia secuencial.");
            ++study.completedWarmupRuns;
            if (index == 0) ++study.completedLocalWarmups;
        }
    }
    study.samples.reserve(2u * config.repetitions);
    for (int round = 0; round < config.repetitions; ++round) {
        for (int position = 0; position < 2; ++position) {
            auto sample = measure((round + static_cast<std::size_t>(position)) % 2);
            sample.round = round;
            sample.position = position;
            if (sample.configurationIndex == 1) sample.completedWarmups = config.warmups;
            study.samples.push_back(std::move(sample));
        }
    }
    ensureActive();
    study.complete = true;
    study.summaries = summarizeStudy(study);
    const auto warmups = study.completedWarmupRuns;
    return {description, std::move(context), ExperimentResult{
        std::move(config), std::move(identity), warmups, {}, {}, std::move(study)}};
}

} // namespace conway::app

#include "app/experiment_runner.hpp"
#include "benchmark_detail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace conway::app {
namespace {
std::size_t checkedAdd(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        throw std::length_error("Experiment size addition overflows");
    return a + b;
}
std::size_t checkedMultiply(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::length_error("Experiment size multiplication overflows");
    return a * b;
}

ExperimentInputIdentity validateInput(const InputData& input) {
    if (!input.width || !input.height)
        throw std::invalid_argument("Experiment dimensions must be positive");
    if (input.height >= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        throw std::length_error("Experiment height exceeds engine index range");
    const auto count = checkedMultiply(input.width, input.height);
    const auto padded = checkedMultiply(checkedAdd(input.width, 2), checkedAdd(input.height, 2));
    if (padded > std::vector<std::uint8_t>{}.max_size())
        throw std::length_error("Experiment halo exceeds vector capacity");
    // Peak board payload: borrowed input + reference Game + current Game.
    (void)checkedAdd(count, checkedMultiply(4, padded));
    if (input.cells.size() != count)
        throw std::invalid_argument("Experiment cell count does not match dimensions");

    std::uint64_t fingerprint = 14695981039346656037ULL;
    const auto append = [&](std::uint8_t byte) {
        fingerprint ^= byte;
        fingerprint *= 1099511628211ULL; // Defined uint64_t wraparound.
    };
    static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
    for (auto dimension : {input.width, input.height}) {
        for (int shift = 0; shift < 64; shift += 8)
            append(static_cast<std::uint8_t>(static_cast<std::uint64_t>(dimension) >> shift));
    }
    std::size_t alive = 0;
    for (auto cell : input.cells) {
        if (cell > 1) throw std::invalid_argument("Experiment requires binary cells");
        alive += cell;
        append(cell);
    }
    return {input.width, input.height, alive, fingerprint, std::nullopt};
}

Engine engineAt(MeasurementOrder order, int round, int position) {
    const bool sequentialFirst = (order == MeasurementOrder::AlternatingSequentialFirst)
                                 == (round % 2 == 0);
    return (sequentialFirst == (position == 0)) ? Engine::Sequential : Engine::OpenMP;
}

ExperimentResult execute(const InputData& input, const ExperimentConfig& config,
                         const input::GenerationMetadata* generated) {
    if (config.generations <= 0 || config.warmups < 0 || config.repetitions <= 0
        || config.requestedThreads <= 0)
        throw std::invalid_argument("Invalid experiment generations, warmups, repetitions or threads");
    if (config.order != MeasurementOrder::AlternatingSequentialFirst
        && config.order != MeasurementOrder::AlternatingOpenMPFirst)
        throw std::invalid_argument("Unknown experiment order policy");
    const auto warmupCount = checkedMultiply(static_cast<std::size_t>(config.warmups), 2);
    const auto sampleCount = checkedMultiply(static_cast<std::size_t>(config.repetitions), 2);
    (void)checkedAdd(1, checkedAdd(warmupCount, sampleCount));
    if (sampleCount > std::vector<ExperimentSample>{}.max_size())
        throw std::length_error("Experiment sample count exceeds vector capacity");
    auto identity = validateInput(input);
    if (generated) {
        const double observed = static_cast<double>(identity.aliveCount) / static_cast<double>(input.cells.size());
        if (generated->width != input.width || generated->height != input.height
            || generated->alive_count != identity.aliveCount
            || generated->requested_density < 0 || generated->requested_density > 10000
            || generated->observed_density != observed || generated->version.empty())
            throw std::invalid_argument("Generated input metadata does not match input");
        identity.generated = GeneratedInputIdentity{generated->seed, generated->requested_density,
                                                    observed, std::string(generated->version)};
    }
    ExperimentResult result{config, std::move(identity), 0, {}, {}};
    result.samples.reserve(sampleCount);

    detail::OpenMPSettingsGuard settings;
    omp_set_dynamic(0);
    const auto reference = detail::runOnce(input, Engine::Sequential, 1, config.generations);
    for (int round = 0; round < config.warmups; ++round) {
        for (int position = 0; position < 2; ++position) {
            (void)detail::runOnce(input, engineAt(config.order, round, position),
                                  config.requestedThreads, config.generations);
            ++result.completedWarmupRuns;
        }
    }
    std::size_t executionIndex = 1 + result.completedWarmupRuns;
    for (int round = 0; round < config.repetitions; ++round) {
        for (int position = 0; position < 2; ++position) {
            const auto engine = engineAt(config.order, round, position);
            auto run = detail::runOnce(input, engine, config.requestedThreads, config.generations);
            const bool equivalent = detail::sameState(run.finalGame, reference.finalGame);
            result.samples.push_back({
                {engine, round, run.elapsedMs, config.generations,
                 engine == Engine::OpenMP ? std::optional<int>{config.requestedThreads} : std::nullopt,
                 run.minEffectiveThreads, run.maxEffectiveThreads, equivalent},
                round, position, executionIndex++});
        }
    }
    result.summary = summarizeExperimentSamples(result.samples);
    return result;
}
} // namespace

bool ExperimentSample::validForComparison() const noexcept {
    const auto& sample = measurement;
    const bool validTime = std::isfinite(sample.elapsedMs) && sample.elapsedMs >= 0;
    if (!validTime || !sample.equivalent || sample.generations <= 0) return false;
    if (sample.engine == Engine::Sequential)
        return !sample.requestedThreads && sample.minEffectiveThreads == 1 && sample.maxEffectiveThreads == 1;
    return sample.engine == Engine::OpenMP && sample.requestedThreads && *sample.requestedThreads > 0
        && sample.minEffectiveThreads > 0 && sample.maxEffectiveThreads >= sample.minEffectiveThreads;
}

ExperimentSummary summarizeExperimentSamples(std::span<const ExperimentSample> samples) {
    ExperimentSummary summary;
    if (samples.empty()) return summary;
    summary.equivalent = true;
    bool valid = true, sequentialTimesValid = true, parallelTimesValid = true;
    std::vector<double> sequentialTimes, parallelTimes;
    std::optional<int> requested;
    const int generations = samples.front().measurement.generations;
    for (const auto& sample : samples) {
        const auto& observed = sample.measurement;
        summary.equivalent = summary.equivalent && observed.equivalent;
        valid = valid && sample.validForComparison() && observed.generations == generations;
        const bool timeValid = std::isfinite(observed.elapsedMs) && observed.elapsedMs >= 0;
        if (observed.engine == Engine::Sequential) {
            sequentialTimesValid = sequentialTimesValid && timeValid;
            sequentialTimes.push_back(observed.elapsedMs);
        } else if (observed.engine == Engine::OpenMP) {
            parallelTimesValid = parallelTimesValid && timeValid;
            parallelTimes.push_back(observed.elapsedMs);
            if (requested && requested != observed.requestedThreads) valid = false;
            requested = observed.requestedThreads;
            summary.minEffectiveThreads = summary.minEffectiveThreads
                ? std::min(*summary.minEffectiveThreads, observed.minEffectiveThreads) : observed.minEffectiveThreads;
            summary.maxEffectiveThreads = summary.maxEffectiveThreads
                ? std::max(*summary.maxEffectiveThreads, observed.maxEffectiveThreads) : observed.maxEffectiveThreads;
        }
    }
    if (sequentialTimesValid) summary.sequentialStatistics = calculateBenchmarkStatistics(sequentialTimes);
    if (parallelTimesValid) summary.openMPStatistics = calculateBenchmarkStatistics(parallelTimes);
    summary.validForComparison = valid && !sequentialTimes.empty()
        && sequentialTimes.size() == parallelTimes.size();
    summary.effectiveThreadCountStable = summary.minEffectiveThreads && summary.maxEffectiveThreads
        && *summary.minEffectiveThreads > 0 && summary.minEffectiveThreads == summary.maxEffectiveThreads;
    if (summary.validForComparison && summary.sequentialStatistics && summary.openMPStatistics
        && summary.sequentialStatistics->medianMs > 0 && summary.openMPStatistics->medianMs > 0) {
        const double ratio = summary.sequentialStatistics->medianMs / summary.openMPStatistics->medianMs;
        if (std::isfinite(ratio) && ratio > 0) summary.speedup = ratio;
    }
    if (summary.speedup && summary.effectiveThreadCountStable) {
        const double efficiency = *summary.speedup / *summary.minEffectiveThreads;
        if (std::isfinite(efficiency) && efficiency > 0) summary.efficiency = efficiency;
    }
    return summary;
}

ExperimentResult runExperiment(const InputData& input, const ExperimentConfig& config) {
    return execute(input, config, nullptr);
}
ExperimentResult runExperiment(const input::GeneratedInput& input, const ExperimentConfig& config) {
    return execute(input.input, config, &input.metadata);
}
} // namespace conway::app

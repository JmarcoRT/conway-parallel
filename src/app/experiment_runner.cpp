#include "app/experiment_runner.hpp"
#include "benchmark_detail.hpp"
#include "experiment_input_detail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>

namespace conway::app {
namespace {
using detail::checkedAdd;
using detail::checkedMultiply;
using detail::validateInput;

Engine engineAt(MeasurementOrder order, int round, int position) {
    const bool sequentialFirst = (order == MeasurementOrder::AlternatingSequentialFirst)
                                 == (round % 2 == 0);
    return (sequentialFirst == (position == 0)) ? Engine::Sequential : Engine::OpenMP;
}

ExperimentResult execute(const InputData& input, const ExperimentConfig& config,
                         const input::GenerationMetadata* generated) {
    if (config.generations <= 0 || config.warmups < 0 || config.repetitions <= 0
        || (config.participants.empty() && config.requestedThreads <= 0))
        throw std::invalid_argument("Invalid experiment generations, warmups, repetitions or threads");
    if (config.order != MeasurementOrder::AlternatingSequentialFirst
        && config.order != MeasurementOrder::AlternatingOpenMPFirst)
        throw std::invalid_argument("Unknown experiment order policy");
    if(!config.participants.empty()) {
        std::set<Engine> engines;
        for(const auto& participant:config.participants)
            if(!engines.insert(participant.engine).second)
                throw std::invalid_argument("Comparison permits one configuration per engine");
        auto identity=validateInput(input);
        detail::attachGeneratedMetadata(identity,input,generated);
        ExperimentResult result{config,std::move(identity),0,{},{}};
        result.study=runExperimentalStudy(input,config.participants,config.generations,
            config.warmups,config.repetitions,
            config.rotateRight,config.mpiOptions,config.maxMemoryBytes);
        result.completedWarmupRuns=result.study->completedWarmupRuns;
        return result;
    }
    const auto warmupCount = checkedMultiply(static_cast<std::size_t>(config.warmups), 2);
    const auto sampleCount = checkedMultiply(static_cast<std::size_t>(config.repetitions), 2);
    (void)checkedAdd(1, checkedAdd(warmupCount, sampleCount));
    if (sampleCount > std::vector<ExperimentSample>{}.max_size())
        throw std::length_error("Experiment sample count exceeds vector capacity");
    auto identity = validateInput(input);
    detail::attachGeneratedMetadata(identity, input, generated);
    ExperimentResult result{config, std::move(identity), 0, {}, {}};
    result.samples.reserve(sampleCount);

    detail::OpenMPSettingsGuard settings;
    omp_set_dynamic(0);
    const auto reference = detail::runOnce(input, Engine::Sequential, 1, config.generations);
    for (int round = 0; round < config.warmups; ++round) {
        for (int position = 0; position < 2; ++position) {
            const auto warmed=detail::runOnce(input, engineAt(config.order, round, position),
                                  config.requestedThreads, config.generations);
            if(!detail::sameState(warmed.finalGame,reference.finalGame))
                throw std::runtime_error("Warmup differs from sequential oracle");
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
    // Reserve exact per-engine counts: bounded workspace for scaling summaries.
    sequentialTimes.reserve(static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(),
        [](const auto& sample) { return sample.measurement.engine == Engine::Sequential; })));
    parallelTimes.reserve(static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(),
        [](const auto& sample) { return sample.measurement.engine == Engine::OpenMP; })));
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

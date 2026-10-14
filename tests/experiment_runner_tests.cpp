#include "app/experiment_runner.hpp"

#include <omp.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

// Deterministic allocation failure in the first 5x5 Game's 7x7 buffer.
// Never enabled during OpenMP work; no fault-injection hooks in production.
namespace { bool failGameAllocation = false; }
void* operator new(std::size_t size) {
    if (failGameAllocation && size == 49) {
        failGameAllocation = false;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
int failures = 0;
void check(bool condition, const char* label) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
template<class Exception, class Function>
void rejects(Function action, const char* label) {
    try { action(); check(false, label); }
    catch (const Exception&) {}
    catch (...) { check(false, label); }
}
using namespace conway;
using namespace conway::app;

void verifyResult(const ExperimentResult& result, int effective) {
    const auto& config = result.config;
    check(result.samples.size() == static_cast<std::size_t>(2 * config.repetitions),
          "one measured sample per engine per round, excludes warmups/reference");
    check(result.completedWarmupRuns == static_cast<std::size_t>(2 * config.warmups),
          "configured warmups per engine");
    std::vector<double> sequential, parallel;
    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const auto& sample = result.samples[index];
        const auto& measurement = sample.measurement;
        const int round = static_cast<int>(index / 2);
        const int position = static_cast<int>(index % 2);
        const bool sequentialFirst = config.order == MeasurementOrder::AlternatingSequentialFirst;
        const bool expectedSequential = ((round % 2 == 0) == sequentialFirst) == (position == 0);
        check(measurement.engine == (expectedSequential ? Engine::Sequential : Engine::OpenMP),
              "alternating chronological acquisition order");
        check(sample.roundIndex == round && sample.positionInRound == position
              && measurement.repetitionIndex == round
              && sample.executionIndex == 1 + result.completedWarmupRuns + index,
              "auditable round and global execution positions");
        check(measurement.generations == config.generations && measurement.equivalent
              && std::isfinite(measurement.elapsedMs) && measurement.elapsedMs >= 0
              && sample.validForComparison(), "individual complete batch and equivalence");
        check(expectedSequential ? !measurement.requestedThreads
                                 : measurement.requestedThreads == config.requestedThreads,
              "requested threads are engine-specific");
        check(measurement.minEffectiveThreads == (expectedSequential ? 1 : effective)
              && measurement.maxEffectiveThreads == (expectedSequential ? 1 : effective),
              "effective threads are observed, including runtime restriction");
        (expectedSequential ? sequential : parallel).push_back(measurement.elapsedMs);
    }
    const auto seq = calculateBenchmarkStatistics(sequential);
    const auto omp = calculateBenchmarkStatistics(parallel);
    const auto& summary = result.summary;
    check(summary.equivalent && summary.validForComparison && summary.effectiveThreadCountStable
          && summary.minEffectiveThreads == effective && summary.maxEffectiveThreads == effective,
          "valid comparison and measured team summary");
    check(seq && omp && summary.sequentialStatistics && summary.openMPStatistics,
          "statistics available for both measured series");
    if (seq && omp && summary.sequentialStatistics && summary.openMPStatistics) {
        const auto same = [](const auto& a, const auto& b) {
            return a.minimumMs == b.minimumMs && a.maximumMs == b.maximumMs
                && a.medianMs == b.medianMs && a.meanMs == b.meanMs
                && a.populationStddevMs == b.populationStddevMs;
        };
        check(same(*seq, *summary.sequentialStatistics) && same(*omp, *summary.openMPStatistics),
              "all statistics use precisely the original measured times");
        if (seq->medianMs > 0 && omp->medianMs > 0) {
            check(summary.speedup == seq->medianMs / omp->medianMs
                  && summary.efficiency == *summary.speedup / effective,
                  "derived medians and efficiency use effective team");
        } else {
            check(!summary.speedup && !summary.efficiency, "zero resolution is not a speedup");
        }
    }
}

void syntheticSummaries() {
    std::vector<ExperimentSample> samples{
        {{Engine::Sequential,0,8,5,std::nullopt,1,1,true},0,0,1},
        {{Engine::OpenMP,0,2,5,4,2,2,true},0,1,2},
        {{Engine::OpenMP,1,4,5,4,2,2,true},1,0,3},
        {{Engine::Sequential,1,10,5,std::nullopt,1,1,true},1,1,4}};
    const auto valid = summarizeExperimentSamples(samples);
    check(valid.validForComparison && valid.equivalent && valid.speedup == 3
          && valid.efficiency == 1.5 && valid.sequentialStatistics->medianMs == 9
          && valid.openMPStatistics->medianMs == 3, "synthetic exact metrics");
    samples[1].measurement.equivalent = false;
    const auto mismatch = summarizeExperimentSamples(samples);
    check(!mismatch.equivalent && !mismatch.validForComparison && !mismatch.speedup
          && !mismatch.efficiency && mismatch.openMPStatistics.has_value(),
          "obtained mismatched measurement retains descriptive stats but invalidates metrics");
    samples[1].measurement.equivalent = true;
    samples[1].measurement.maxEffectiveThreads = 3;
    const auto variable = summarizeExperimentSamples(samples);
    check(variable.validForComparison && variable.speedup == 3 && !variable.efficiency
          && !variable.effectiveThreadCountStable && variable.minEffectiveThreads == 2
          && variable.maxEffectiveThreads == 3, "varying teams retain evidence without efficiency");
    samples[1].measurement.maxEffectiveThreads = 2;
    samples[2].measurement.minEffectiveThreads = samples[2].measurement.maxEffectiveThreads = 3;
    check(!summarizeExperimentSamples(samples).efficiency, "team changes between samples invalidate efficiency");
    samples[2].measurement.minEffectiveThreads = samples[2].measurement.maxEffectiveThreads = 2;
    samples[0].measurement.generations = 6;
    check(!summarizeExperimentSamples(samples).speedup, "different workloads cannot be compared");
    samples[0].measurement.generations = 5;
    samples[1].measurement.requestedThreads = 8;
    check(!summarizeExperimentSamples(samples).speedup, "different thread requests cannot be pooled");
    samples[1].measurement.requestedThreads = 4;
    samples[1].measurement.minEffectiveThreads = 0;
    check(!summarizeExperimentSamples(samples).speedup, "invalid effective observation rejected");
    samples[1].measurement.minEffectiveThreads = 2;
    for (auto& sample : samples) sample.measurement.elapsedMs = 0;
    const auto zero = summarizeExperimentSamples(samples);
    check(zero.validForComparison && zero.sequentialStatistics->medianMs == 0
          && !zero.speedup && !zero.efficiency, "zero times are available observations, unavailable metrics");
    for (double time : {-1.0, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
        samples[1].measurement.elapsedMs = time;
        const auto invalid = summarizeExperimentSamples(samples);
        check(!invalid.validForComparison && !invalid.speedup && !invalid.openMPStatistics
              && invalid.sequentialStatistics.has_value(), "invalid time never silently dropped from stats");
    }
    const auto empty = summarizeExperimentSamples({});
    check(!empty.validForComparison && !empty.speedup && !empty.efficiency
          && !empty.sequentialStatistics && !empty.minEffectiveThreads, "empty summary unavailable");
    samples.resize(1);
    check(!summarizeExperimentSamples(samples).speedup, "missing engine cannot be compared");
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const bool limited = argc > 2;
    const int effective = limited ? 1 : 2;
    const auto input = loadInput(std::string(argv[1]) + "/patterns/blinker.txt");
    const auto original = input.cells;
    omp_set_num_threads(3);
    omp_set_dynamic(1);
    const int previousThreads = omp_get_max_threads(), previousDynamic = omp_get_dynamic();
    for (auto config : {ExperimentConfig{1,0,1,2}, {5,1,2,2},
                        {8,2,3,2,MeasurementOrder::AlternatingOpenMPFirst}}) {
        const auto result = runExperiment(input, config);
        verifyResult(result, effective);
        check(result.config.generations == config.generations && result.config.warmups == config.warmups
              && result.config.repetitions == config.repetitions && result.config.order == config.order
              && result.config.requestedThreads == config.requestedThreads, "configuration retained");
        check(!result.input.generated && input.cells == original, "existing input borrowed without mutation");
        check(omp_get_max_threads() == previousThreads && omp_get_dynamic() == previousDynamic,
              "caller OpenMP settings restored after success");
        const auto again = runExperiment(input, config);
        check(again.input.fingerprint == result.input.fingerprint, "stable existing input identity");
        for (std::size_t i = 0; i < result.samples.size(); ++i)
            check(again.samples[i].measurement.engine == result.samples[i].measurement.engine
                  && again.samples[i].executionIndex == result.samples[i].executionIndex,
                  "repeated configuration has reproducible schedule, not assumed equal times");
    }
    const auto generated = conway::input::generateInput({31,17,3000,0});
    const auto saved = generated.input.cells;
    const auto result = runExperiment(generated, {7,1,4,2});
    verifyResult(result, effective);
    check(result.input.generated && result.input.generated->seed == 0
          && result.input.generated->requestedDensity == 3000
          && result.input.generated->observedDensity == generated.metadata.observed_density
          && result.input.generated->generatorVersion == generated.metadata.version
          && result.input.width == 31 && result.input.height == 17
          && result.input.aliveCount == generated.metadata.alive_count
          && saved == generated.input.cells, "generated provenance and same immutable initial input");
    check(runExperiment(generated.input, {1,0,1,2}).input.fingerprint == result.input.fingerprint,
          "generated and raw overload identify identical cells");
    const auto owned = [] {
        auto generated = conway::input::generateInput({1,1,0,42});
        std::string temporaryVersion(generated.metadata.version);
        generated.metadata.version = temporaryVersion;
        return runExperiment(generated, {2,0,1,1});
    }();
    check(owned.input.generated->generatorVersion == conway::input::kGeneratorVersion,
          "result owns provenance after input and version lifetime ends");
    auto altered = generated.input;
    altered.cells[0] ^= 1;
    check(runExperiment(altered, {1,0,1,2}).input.fingerprint != result.input.fingerprint,
          "content changes input fingerprint");
    const auto allDead = conway::input::generateInput({1,1,0,0});
    check(runExperiment(allDead,{1,0,1,1}).input.fingerprint == 10560200489033160511ULL,
          "portable fingerprint golden case with little-endian dimensions");
    const auto allAlive = conway::input::generateInput({1,1,10000,0});
    check(runExperiment(allDead,{2,0,1,1}).summary.equivalent
          && runExperiment(allAlive,{2,0,1,1}).summary.equivalent, "minimum and density endpoints");

    for (auto config : {ExperimentConfig{0,0,1,1}, {-1,0,1,1}, {1,-1,1,1},
                        {1,0,0,1}, {1,0,-1,1}, {1,0,1,0}, {1,0,1,-1},
                        {1,0,1,1,static_cast<MeasurementOrder>(99)}})
        rejects<std::invalid_argument>([&] { runExperiment(input,config); }, "invalid configuration rejected");
    for (auto invalid : {InputData{0,1,{}}, {1,0,{}}, {2,2,{0}}, {1,1,{2}}, {1,1,{255}}})
        rejects<std::invalid_argument>([&] { runExperiment(invalid,{1,0,1,1}); }, "invalid input rejected");
    const auto max = std::numeric_limits<std::size_t>::max();
    for (auto invalid : {InputData{max,1,{}}, {max/2+1,2,{}},
                        {1,static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()),{}}})
        rejects<std::length_error>([&] { runExperiment(invalid,{1,0,1,1}); }, "unsafe dimensions rejected");
    auto incorrect = conway::input::generateInput({2,2,3000,0});
    ++incorrect.metadata.alive_count;
    rejects<std::invalid_argument>([&] { runExperiment(incorrect,{1,0,1,1}); }, "inconsistent generated metadata rejected");
    check(omp_get_max_threads() == previousThreads && omp_get_dynamic() == previousDynamic,
          "OpenMP settings unchanged on invalid input");

    const InputData faultInput{5,5,std::vector<std::uint8_t>(25,0)};
    failGameAllocation = true;
    rejects<std::bad_alloc>([&] { runExperiment(faultInput,{1,0,1,2}); }, "allocation failure propagated inside guarded execution");
    check(!failGameAllocation && omp_get_max_threads() == previousThreads
          && omp_get_dynamic() == previousDynamic, "OpenMP settings restored on exception after guard activation");
    failGameAllocation = false;
    syntheticSummaries();
    const auto legacy = runBenchmark(input,3,2,2);
    check(legacy.equivalent && legacy.samples.size() == 4
          && legacy.samples[0].engine == Engine::Sequential && legacy.samples[1].engine == Engine::Sequential
          && legacy.samples[2].engine == Engine::OpenMP && legacy.samples[3].engine == Engine::OpenMP,
          "legacy benchmark preserves grouped order and equivalent states");
    check(omp_get_max_threads() == previousThreads && omp_get_dynamic() == previousDynamic,
          "legacy also preserves caller OpenMP settings");
    std::cout << (failures ? "FAIL" : "PASS") << ": experimental runner (" << failures << " failures)\n";
    return failures ? 1 : 0;
}

#include "app/scaling_study.hpp"

#include <omp.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace {
std::atomic<bool> trackGameBuffers{false}, failGameBuffer{false};
std::atomic<std::size_t> gameBufferCount{0};
}
// 5x5 fixtures have 49-byte halo buffers. Observe construction outside timing,
// and inject a deterministic exception without production test hooks.
void* operator new(std::size_t size) {
    if (size == 49) {
        if (failGameBuffer.exchange(false)) throw std::bad_alloc();
        if (trackGameBuffers.load()) gameBufferCount.fetch_add(1);
    }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using namespace conway;
using namespace conway::app;
int failures = 0;
void check(bool value, const char* label) {
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
template<class Exception, class Function>
void rejects(Function action, const char* label) {
    try { action(); check(false,label); }
    catch (const Exception&) {}
    catch (...) { check(false,label); }
}
bool sameStats(const BenchmarkStatistics& a, const BenchmarkStatistics& b) {
    return a.minimumMs == b.minimumMs && a.maximumMs == b.maximumMs
        && a.medianMs == b.medianMs && a.meanMs == b.meanMs
        && a.populationStddevMs == b.populationStddevMs;
}

void verify(const ScalingResult& result, bool limited) {
    const auto& config = result.config;
    const auto count = config.requestedThreads.size() + 1;
    check(result.samples.size() == count * static_cast<std::size_t>(config.repetitions),
          "exactly one sample per configuration per round");
    check(result.completedWarmupRuns == count * static_cast<std::size_t>(config.warmups),
          "warmups counted separately, oracle excluded");
    check(result.estimatedPeakBytes <= config.maxMemoryBytes, "total payload within configured budget");
    std::vector<std::vector<double>> times(count);
    std::vector<std::vector<int>> positions(count, std::vector<int>(count,0));
    std::size_t sequentialCount = 0;
    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const auto& sample = result.samples[index];
        const auto& execution = sample.execution;
        const auto& measurement = execution.measurement;
        const auto round = index / count, position = index % count;
        const auto expected = config.order == ScalingOrder::RotateLeft
            ? (position + round % count) % count : (position + count - round % count) % count;
        check(sample.configurationIndex == expected && execution.roundIndex == static_cast<int>(round)
              && execution.positionInRound == static_cast<int>(position)
              && measurement.repetitionIndex == static_cast<int>(round)
              && execution.executionIndex == 1 + result.completedWarmupRuns + index,
              "rotation, round and global acquisition indices reconstruct execution");
        check(measurement.generations == config.generations && measurement.equivalent
              && std::isfinite(measurement.elapsedMs) && measurement.elapsedMs >= 0,
              "unrounded complete batch and correct final state");
        check(sample.sequentialReferenceIndex < result.samples.size(), "reference index within samples");
        if (sample.sequentialReferenceIndex < result.samples.size()) {
            const auto& reference = result.samples[sample.sequentialReferenceIndex];
            check(reference.configurationIndex == 0 && reference.execution.roundIndex == execution.roundIndex
                  && sample.validForComparison(reference), "same-round shared Sequential reference is valid");
        }
        if (!expected) {
            ++sequentialCount;
            check(measurement.engine == Engine::Sequential && !measurement.requestedThreads
                  && measurement.minEffectiveThreads == 1 && measurement.maxEffectiveThreads == 1,
                  "one Sequential execution and its thread metadata");
        } else {
            const int request = static_cast<int>(config.requestedThreads[expected - 1]);
            const int effective = limited ? 1 : request;
            check(measurement.engine == Engine::OpenMP && measurement.requestedThreads == request
                  && measurement.minEffectiveThreads == effective && measurement.maxEffectiveThreads == effective,
                  "each requested configuration keeps its observed team");
        }
        times[sample.configurationIndex].push_back(measurement.elapsedMs);
        ++positions[sample.configurationIndex][position];
    }
    check(sequentialCount == static_cast<std::size_t>(config.repetitions),
          "Sequential series measured once per round, not once per thread configuration");
    if (static_cast<std::size_t>(config.repetitions) >= count) {
        for (const auto& histogram : positions) {
            const auto [min,max] = std::minmax_element(histogram.begin(), histogram.end());
            check(*max - *min <= 1, "rotation distributes all positions within one occurrence");
        }
    }
    const auto sequential = calculateBenchmarkStatistics(times[0]);
    check(result.summary.equivalent && result.summary.validForComparison
          && result.summary.sequentialStatistics && sequential
          && sameStats(*result.summary.sequentialStatistics,*sequential),
          "shared Sequential statistics include only measured samples");
    check(result.summary.points.size() == config.requestedThreads.size(), "one summary per configurable request");
    for (std::size_t point = 0; point < result.summary.points.size(); ++point) {
        const auto& entry = result.summary.points[point];
        const auto& summary = entry.summary;
        const auto parallel = calculateBenchmarkStatistics(times[point + 1]);
        const int effective = limited ? 1 : static_cast<int>(config.requestedThreads[point]);
        check(entry.requestedThreads == config.requestedThreads[point]
              && summary.equivalent && summary.validForComparison && summary.effectiveThreadCountStable
              && summary.minEffectiveThreads == effective && summary.maxEffectiveThreads == effective,
              "point identity and effective-team summary");
        check(summary.sequentialStatistics && summary.openMPStatistics && parallel && sequential
              && sameStats(*summary.sequentialStatistics,*sequential)
              && sameStats(*summary.openMPStatistics,*parallel), "statistics reuse the same Sequential series");
        if (sequential && parallel && sequential->medianMs > 0 && parallel->medianMs > 0) {
            check(summary.speedup == sequential->medianMs / parallel->medianMs
                  && summary.efficiency == *summary.speedup / effective,
                  "median speedup and effective-thread efficiency formulas");
        } else {
            check(!summary.speedup && !summary.efficiency, "unresolved zero times have unavailable metrics");
        }
    }
}

void syntheticRules() {
    ScalingConfig config{5,0,2,{2,7}};
    // Left rotation: S,O2,O7; O2,O7,S. Sequential medians=9; O2=3; O7=1.5.
    std::vector<ScalingSample> samples{
        {{{Engine::Sequential,0,8,5,std::nullopt,1,1,true},0,0,1},0,0},
        {{{Engine::OpenMP,0,2,5,2,2,2,true},0,1,2},1,0},
        {{{Engine::OpenMP,0,1,5,7,3,3,true},0,2,3},2,0},
        {{{Engine::OpenMP,1,4,5,2,2,2,true},1,0,4},1,5},
        {{{Engine::OpenMP,1,2,5,7,3,3,true},1,1,5},2,5},
        {{{Engine::Sequential,1,10,5,std::nullopt,1,1,true},1,2,6},0,5}};
    const auto exact = summarizeScalingSamples(samples,config);
    check(exact.validForComparison && exact.points[0].summary.speedup == 3
          && exact.points[0].summary.efficiency == 1.5 && exact.points[1].summary.speedup == 6
          && exact.points[1].summary.efficiency == 2, "synthetic scaling formulas with shared medians");
    samples[1].execution.measurement.equivalent = false;
    const auto mismatch = summarizeScalingSamples(samples,config);
    check(!mismatch.equivalent && !mismatch.validForComparison
          && !mismatch.points[0].summary.speedup && mismatch.points[0].summary.openMPStatistics
          && mismatch.points[1].summary.speedup == 6, "invalid point retains observations, other valid points remain distinct");
    samples[1].execution.measurement.equivalent = true;
    samples[0].execution.measurement.equivalent = false;
    const auto badReference = summarizeScalingSamples(samples,config);
    check(!badReference.points[0].summary.speedup && !badReference.points[1].summary.speedup
          && !samples[1].validForComparison(samples[0]), "failed shared reference invalidates every dependent point");
    samples[0].execution.measurement.equivalent = true;
    samples[1].execution.measurement.maxEffectiveThreads = 3;
    const auto varying = summarizeScalingSamples(samples,config);
    check(varying.validForComparison && varying.points[0].summary.speedup == 3
          && !varying.points[0].summary.efficiency && varying.points[0].summary.minEffectiveThreads == 2
          && varying.points[0].summary.maxEffectiveThreads == 3, "variable team preserves speedup evidence but no efficiency");
    samples[1].execution.measurement.maxEffectiveThreads = 2;
    samples[3].execution.measurement.minEffectiveThreads = samples[3].execution.measurement.maxEffectiveThreads = 4;
    check(!summarizeScalingSamples(samples,config).points[0].summary.efficiency,
          "team variability across rounds also invalidates efficiency");
    samples[3].execution.measurement.minEffectiveThreads = samples[3].execution.measurement.maxEffectiveThreads = 2;
    samples[1].execution.measurement.elapsedMs = std::numeric_limits<double>::quiet_NaN();
    const auto invalidTime = summarizeScalingSamples(samples,config);
    check(!invalidTime.points[0].summary.speedup && !invalidTime.points[0].summary.openMPStatistics
          && invalidTime.points[1].summary.speedup == 6, "invalid time is never silently discarded");
    samples[1].execution.measurement.elapsedMs = 0;
    samples[3].execution.measurement.elapsedMs = 0;
    check(!summarizeScalingSamples(samples,config).points[0].summary.speedup,
          "zero OpenMP median does not become a metric");
    samples[1].sequentialReferenceIndex = 5;
    rejects<std::invalid_argument>([&] { summarizeScalingSamples(samples,config); }, "malformed round reference rejected");
    rejects<std::invalid_argument>([&] { summarizeScalingSamples({},config); }, "missing samples rejected");
}

void largeStudy() {
    for (std::size_t size : {1000,2000}) {
        const auto generated = conway::input::generateInput({size,size,3000,42});
        const ScalingConfig config{2,1,3,{1,2,4,8}};
        const auto result = runScalingStudy(generated,config);
        verify(result,false);
        check(result.input.generated && result.input.generated->seed == 42
              && result.input.width == size && result.input.height == size
              && result.input.aliveCount == generated.metadata.alive_count, "controlled large generated input identity");
        std::cout << "Large " << size << 'x' << size << ": " << result.samples.size()
                  << " measured batches, estimated payload " << result.estimatedPeakBytes << " bytes\n";
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (argc > 2 && std::string(argv[2]) == "--large") {
        largeStudy();
        std::cout << (failures ? "FAIL" : "PASS") << ": controlled large scaling studies\n";
        return failures ? 1 : 0;
    }
    const bool limited = argc > 2 && std::string(argv[2]) == "--thread-limit-one";
    const auto input = loadInput(std::string(argv[1]) + "/patterns/blinker.txt");
    const auto original = input.cells;
    omp_set_num_threads(3);
    omp_set_dynamic(1);
    const auto previousThreads = omp_get_max_threads(), previousDynamic = omp_get_dynamic();
    for (auto config : {ScalingConfig{1,0,1,{3}}, {5,1,4,{1,3,2}},
                        {6,2,5,{4,1,2},ScalingOrder::RotateRight}}) {
        const auto result = runScalingStudy(input,config);
        verify(result,limited);
        check(result.config.requestedThreads == config.requestedThreads
              && result.config.order == config.order && input.cells == original && !result.input.generated,
              "requested list/config retained and same immutable input reused");
        const auto again = runScalingStudy(input,config);
        check(again.input.fingerprint == result.input.fingerprint, "unchanged input identification");
        for (std::size_t i = 0; i < result.samples.size(); ++i)
            check(again.samples[i].configurationIndex == result.samples[i].configurationIndex
                  && again.samples[i].sequentialReferenceIndex == result.samples[i].sequentialReferenceIndex
                  && again.samples[i].execution.executionIndex == result.samples[i].execution.executionIndex,
                  "schedule is reproducible independently of elapsed times");
        check(omp_get_max_threads() == previousThreads && omp_get_dynamic() == previousDynamic,
              "OpenMP caller settings restored");
    }
    auto generated = conway::input::generateInput({5,5,3000,0});
    const auto saved = generated.input.cells;
    const ScalingConfig config{3,1,4,{1,3,2}};
    gameBufferCount = 0;
    trackGameBuffers = true;
    const auto result = runScalingStudy(generated,config);
    trackGameBuffers = false;
    verify(result,limited);
    check(gameBufferCount == 2 * (1 + 4 * (1 + 4)),
          "actual Game allocations prove one oracle and one execution per configuration/round");
    check(saved == generated.input.cells && result.input.generated && result.input.generated->seed == 0
          && result.input.generated->generatorVersion == generated.metadata.version,
          "generated provenance retained without regenerating or evolving caller board");
    check(result.estimatedPeakBytes >= 25 + 4 * 49 + result.samples.size()*sizeof(ScalingSample),
          "memory accounts for Games and all result samples, not just generator buffers");
    auto tight = config;
    tight.maxMemoryBytes = result.estimatedPeakBytes;
    check(runScalingStudy(generated,tight).estimatedPeakBytes == tight.maxMemoryBytes,
          "exact estimated payload budget accepted");
    --tight.maxMemoryBytes;
    rejects<std::length_error>([&] { runScalingStudy(generated,tight); }, "one byte below estimated payload rejected");
    auto spareInput = generated.input;
    spareInput.cells.reserve(10000);
    const auto raw = runScalingStudy(generated.input,config);
    const auto spare = runScalingStudy(spareInput,config);
    check(spare.estimatedPeakBytes == raw.estimatedPeakBytes
              + spareInput.cells.capacity() - generated.input.cells.capacity()
          && spare.input.fingerprint == raw.input.fingerprint,
          "existing unused input capacity is budgeted without changing identity");
    auto rawBudget = config;
    rawBudget.maxMemoryBytes = raw.estimatedPeakBytes;
    rejects<std::length_error>([&] { runScalingStudy(spareInput,rawBudget); },
                               "excess input capacity cannot bypass total budget");
    auto spareConfig = config;
    spareConfig.requestedThreads.reserve(100);
    check(runScalingStudy(generated.input,spareConfig).estimatedPeakBytes == raw.estimatedPeakBytes
              + (spareConfig.requestedThreads.capacity() - config.requestedThreads.capacity()) * sizeof(std::int64_t),
          "caller configuration spare capacity is included in memory estimate");
    failGameBuffer = true;
    rejects<std::bad_alloc>([&] { runScalingStudy(generated,config); }, "allocation failure propagated inside guarded study");
    check(!failGameBuffer && omp_get_max_threads() == previousThreads && omp_get_dynamic() == previousDynamic,
          "OpenMP restored after allocation exception");
    failGameBuffer = false;

    for (auto invalid : {ScalingConfig{0,0,1,{1}}, {-1,0,1,{1}}, {1,-1,1,{1}},
                         {1,0,0,{1}}, {1,0,-1,{1}}, {1,0,1,{}}, {1,0,1,{0}},
                         {1,0,1,{-1}}, {1,0,1,{2,2}},
                         {1,0,1,{static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1}},
                         {1,0,1,{std::numeric_limits<std::int64_t>::max()}},
                         {1,0,1,{1},static_cast<ScalingOrder>(99)}, {1,0,1,{1},ScalingOrder::RotateLeft,0}})
        rejects<std::invalid_argument>([&] { runScalingStudy(input,invalid); }, "invalid scaling config rejected");
    rejects<std::length_error>([&] {
        runScalingStudy(input,{1,0,std::numeric_limits<int>::max(),{1},ScalingOrder::RotateLeft,1024});
    }, "unreasonable repetition storage rejected by explicit budget before allocation");
    for (auto invalid : {InputData{0,1,{}}, {1,0,{}}, {2,2,{0}}, {1,1,{2}}})
        rejects<std::invalid_argument>([&] { runScalingStudy(invalid,{1,0,1,{1}}); }, "invalid input rejected");
    rejects<std::length_error>([] {
        runScalingStudy(InputData{std::numeric_limits<std::size_t>::max(),1,{}},{1,0,1,{1}});
    }, "halo overflow rejected");
    ++generated.metadata.alive_count;
    rejects<std::invalid_argument>([&] { runScalingStudy(generated,config); }, "bad generated metadata rejected");
    check(omp_get_max_threads() == previousThreads && omp_get_dynamic() == previousDynamic,
          "invalid parameters do not alter OpenMP settings");
    syntheticRules();
    const auto legacy = runBenchmark(input,3,2,2);
    const auto experiment = runExperiment(input,{3,1,2,2});
    check(legacy.equivalent && legacy.samples.size() == 4
          && legacy.samples[0].engine == Engine::Sequential && legacy.samples[1].engine == Engine::Sequential
          && experiment.summary.equivalent && experiment.samples.size() == 4
          && experiment.samples[0].measurement.engine == Engine::Sequential
          && experiment.samples[2].measurement.engine == Engine::OpenMP,
          "old benchmark and alternating experiment remain compatible");
    std::cout << (failures ? "FAIL" : "PASS") << ": scaling studies (" << failures << " failures)\n";
    return failures ? 1 : 0;
}

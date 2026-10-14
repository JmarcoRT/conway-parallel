#include "app/experiment_execution.hpp"
#include <atomic>
#include <future>
#include <iostream>

using namespace conway::app;
int main() {
    int failures = 0;
    auto check = [&](bool value, const char* label) { if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } };
    auto config = *validateExperimentForm(standardExperimentForm(false, 2, false), false, nullptr).config;
    config.generated = conway::input::GenerationConfig{7, 5, 3000, 42};
    config.comparison.generations = config.scaling.generations = 3;
    config.comparison.warmups = config.scaling.warmups = 0;
    config.comparison.repetitions = config.scaling.repetitions = 2;
    config.scaling.requestedThreads = {1,2};
    std::mutex mutex;
    std::condition_variable changed;
    auto notify = [&] { std::lock_guard lock(mutex); changed.notify_all(); };
    ExperimentExecution real(notify);
    auto wait = [&](ExperimentExecution& controller) {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return !controller.snapshot().active(); });
    };
    check(real.snapshot().phase == ExecutionPhase::Ready, "initial ready");
    check(real.start(false, config, nullptr, ""), "generated comparison launch");
    wait(real);
    auto first = real.snapshot();
    check(first.phase == ExecutionPhase::Finished && first.latest, "real result published");
    const auto& comparison = std::get<ExperimentResult>(first.latest->result);
    check(comparison.summary.equivalent && comparison.summary.validForComparison && comparison.samples.size() == 4,
          "real comparison equivalence and measured samples");
    check(comparison.input.generated && comparison.input.generated->seed == 42 && comparison.input.width == 7,
          "generated provenance retained");
    check(!first.latest->context.studyId.empty() && !first.latest->context.fields.empty()
          && !first.latest->context.patternName, "real worker context captured");
    check(!real.publishPhase(first.description.id, ExecutionPhase::Running), "late terminal event ignored");
    InputData loaded{3, 3, {0,1,0,0,1,0,0,1,0}};
    auto loadedConfig = config;
    loadedConfig.generated.reset();
    check(real.start(true, loadedConfig, &loaded, "blinker.txt", loaded.cells.capacity()), "loaded scaling launch");
    wait(real);
    auto second = real.snapshot();
    const auto& scaling = std::get<ScalingResult>(second.latest->result);
    check(scaling.summary.equivalent && scaling.summary.points.size() == 2 && scaling.samples.size() == 6,
          "real scaling execution");
    check(second.latest->context.patternName == "blinker.txt" && !scaling.input.generated, "loaded provenance retained");
    auto tooLarge = config;
    tooLarge.generated = conway::input::GenerationConfig{8000,8000,3000,1};
    check(real.start(false, tooLarge, nullptr, "", 128u*1024u*1024u), "preflight processed");
    auto failed = real.snapshot();
    check(failed.phase == ExecutionPhase::Error && !failed.error.empty() && failed.latest == second.latest,
          "memory budget includes snapshots and preserves earlier result");

    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<int> calls{0};
    std::atomic<bool> copied{false};
    ExperimentExecution controlled(notify, [&](const ExecutionRequest& request, const ExperimentExecution::PhasePublisher& phase) {
        const int call = ++calls;
        if (call == 1) {
            entered.set_value(); gate.wait();
            copied = request.loaded && request.loaded->cells[1] == 1 && request.description.config.comparison.generations == 3;
            phase(ExecutionPhase::Running);
        } else if (call == 2) throw std::runtime_error("controlled failure");
        return StoredExperiment{request.description, {}, ExperimentResult{}};
    });
    controlled.start(false, loadedConfig, &loaded, "original.txt");
    entered.get_future().wait();
    check(controlled.snapshot().phase == ExecutionPhase::Preparing, "real preparation state");
    const auto id = controlled.snapshot().description.id;
    loaded.cells[1] = 0;
    loadedConfig.comparison.generations = 99;
    check(!controlled.start(true, config, nullptr, ""), "simultaneous job rejected");
    check(!controlled.publishPhase(id + 10, ExecutionPhase::Running), "foreign run event ignored");
    release.set_value(); wait(controlled);
    auto good = controlled.snapshot().latest;
    check(copied && calls == 1, "independent snapshot/configuration and no duplicate");
    controlled.start(false, config, nullptr, ""); wait(controlled);
    check(controlled.snapshot().phase == ExecutionPhase::Error && controlled.snapshot().error == "controlled failure"
          && controlled.snapshot().latest == good, "real error message and prior result preserved");
    check(!controlled.publishPhase(id, ExecutionPhase::Running), "old event cannot overwrite a newer job");
    controlled.start(false, config, nullptr, ""); wait(controlled);
    check(controlled.snapshot().phase == ExecutionPhase::Finished && controlled.snapshot().latest != good, "recovery after error");
    std::promise<void> closingEntered, closingRelease;
    auto closingGate = closingRelease.get_future().share();
    ExperimentExecution closing(notify, [&](const ExecutionRequest& request, const ExperimentExecution::PhasePublisher&) {
        closingEntered.set_value(); closingGate.wait();
        return StoredExperiment{request.description, {}, ExperimentResult{}};
    });
    closing.start(false, config, nullptr, ""); closingEntered.get_future().wait();
    auto shutdown = std::async(std::launch::async, [&] { closing.shutdown(); });
    check(closing.snapshot().active(), "shutdown does not cancel active work");
    closingRelease.set_value(); shutdown.get();
    check(closing.snapshot().phase == ExecutionPhase::Finished, "shutdown joins completed worker");
    std::cout << (failures ? "FAIL" : "PASS") << ": async execution, snapshots, IDs, errors, budgets and lifecycle\n";
    return failures ? 1 : 0;
}

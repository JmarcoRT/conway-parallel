#pragma once

#include "app/scaling_study.hpp"
#include <array>
#include <optional>
#include <string>

namespace conway::app {
enum class ExperimentField : std::size_t {
    Width, Height, Density, Seed, Generations, Warmups, Repetitions, Threads, Source, Processes, Participants, Count
};
inline constexpr auto kExperimentFieldCount = static_cast<std::size_t>(ExperimentField::Count);

struct ExperimentFormDraft {
    bool sequential{true}, openmp{false}, mpi{false};
    int parallelEngine{0}; // 0 OpenMP, 1 MPI.
    int source{1}; // 0: current loaded pattern; 1: generated board.
    std::array<std::string, kExperimentFieldCount> values;
};
struct ExperimentFormConfig {
    std::optional<input::GenerationConfig> generated;
    ExperimentConfig comparison;
    ScalingConfig scaling;
};
struct ExperimentFormValidation {
    std::array<std::string, kExperimentFieldCount> errors;
    std::optional<ExperimentFormConfig> config;
};
ExperimentFormDraft standardExperimentForm(bool scaling, int currentThreads, bool hasPattern);
// Pure validation: no generation, Game construction, measurements or OpenMP changes.
ExperimentFormValidation validateExperimentForm(const ExperimentFormDraft& draft, bool scaling,
                                                const InputData* loaded);
} // namespace conway::app

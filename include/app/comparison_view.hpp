#pragma once
#include "app/experiment_runner.hpp"
#include <ftxui/dom/elements.hpp>
#include <array>
#include <string>
#include <vector>

namespace conway::app {
enum class ComparisonTab { Summary, Samples, Method };
struct ComparisonBars {
    bool available{false};
    double maximumMs{};
    std::array<int, 2> cells{};
    std::array<bool, 2> belowCell{};
};
// True only for unambiguous adjacent measured pairs in acquisition order.
bool comparisonSamplesArePaired(const std::vector<ExperimentSample>& samples);
// Presentation geometry only. No statistics or performance metrics are calculated.
ComparisonBars comparisonBars(const ExperimentSummary& summary, int columns);
std::vector<ftxui::Element> comparisonRows(const ExperimentResult& result,
    const std::string& patternName, ComparisonTab tab, int columns,
    ftxui::Color accent, ftxui::Color positive);
} // namespace conway::app

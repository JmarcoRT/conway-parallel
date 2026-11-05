#pragma once
#include "app/scaling_study.hpp"
#include <ftxui/dom/elements.hpp>
#include <string>

namespace conway::app {
enum class ScalingTab { Summary, Samples, Method };
// Numeric requests for OpenMP/MPI; categorical positions with labels for CUDA.
struct ScalingPlotPoint { int requestedThreads; double speedup; std::string label; };
// Presentation copies only: backend order and observations remain untouched.
std::vector<ScalingPlotPoint> scalingPlotPoints(const ScalingResult& result);
std::vector<ftxui::Element> scalingRows(const ScalingResult& result,
    const std::string& patternName, ScalingTab tab, int columns,
    ftxui::Color accent, ftxui::Color positive, bool sampleDetails = false, bool methodDetails = false);
}

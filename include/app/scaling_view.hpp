#pragma once
#include "app/scaling_study.hpp"
#include <ftxui/dom/elements.hpp>

namespace conway::app {
enum class ScalingTab { Summary, Samples, Method };
struct ScalingPlotPoint { int requestedThreads; double speedup; };
// Presentation copies only: backend order and observations remain untouched.
std::vector<ScalingPlotPoint> scalingPlotPoints(const ScalingResult& result);
std::vector<ftxui::Element> scalingRows(const ScalingResult& result,
    const std::string& patternName, ScalingTab tab, int columns,
    ftxui::Color accent, ftxui::Color positive);
}

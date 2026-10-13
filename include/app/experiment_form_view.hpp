#pragma once
#include "input/input.hpp"
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>

namespace conway::app {
// Getters always consult live application state; the view owns only draft text.
ftxui::Component makeExperimentFormView(
    bool scaling, std::function<const InputData*()> input,
    std::function<std::string()> patternName, std::function<int()> currentThreads,
    std::function<ftxui::Element(ftxui::Element)> focusedStyle);
} // namespace conway::app

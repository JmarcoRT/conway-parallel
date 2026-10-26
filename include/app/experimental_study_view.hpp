#pragma once
#include "app/experimental_study.hpp"
#include <ftxui/dom/elements.hpp>
namespace conway::app {
// Presentation only: statistics and validity are consumed, never recomputed.
std::vector<ftxui::Element> experimentalStudyRows(const ExperimentalStudy&,const std::string& pattern,
    int tab,int columns,ftxui::Color accent,ftxui::Color positive);
}

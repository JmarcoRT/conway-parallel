#pragma once
#include "app/experimental_study.hpp"
#include <ftxui/dom/elements.hpp>
namespace conway::app {
// Compact comparison summary; scaling retains its own presentation.
std::vector<ftxui::Element> comparisonStudySummaryRows(const ExperimentalStudy&,const std::string& pattern,
    int columns,ftxui::Color accent,ftxui::Color positive);
std::vector<ftxui::Element> comparisonStudySampleRows(const ExperimentalStudy&,int columns,
    ftxui::Color accent,ftxui::Color positive,bool details);
std::vector<ftxui::Element> comparisonStudyMethodRows(const ExperimentalStudy&,int columns,
    ftxui::Color accent,ftxui::Color positive,bool details);
// Presentation only: statistics and validity are consumed, never recomputed.
std::vector<ftxui::Element> experimentalStudyRows(const ExperimentalStudy&,const std::string& pattern,
    int tab,int columns,ftxui::Color accent,ftxui::Color positive);
}

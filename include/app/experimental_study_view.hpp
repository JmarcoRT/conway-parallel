#pragma once
#include "app/experimental_study.hpp"
#include <ftxui/dom/elements.hpp>
namespace conway::app {
// Shared compact presentation for comparison and per-engine scaling studies.
std::vector<ftxui::Element> studySummaryRows(const ExperimentalStudy&,const std::string& pattern,
    int columns,ftxui::Color accent,ftxui::Color positive,std::optional<Engine> scalingEngine = {});
std::vector<ftxui::Element> studySampleRows(const ExperimentalStudy&,int columns,
    ftxui::Color accent,ftxui::Color positive,bool details,std::optional<Engine> scalingEngine = {});
std::vector<ftxui::Element> studyMethodRows(const ExperimentalStudy&,int columns,
    ftxui::Color accent,ftxui::Color positive,bool details,std::optional<Engine> scalingEngine = {});
}

#pragma once
#include "app/cuda_didactic_session.hpp"
#include "ui/responsive_layout.hpp"
#include <ftxui/dom/elements.hpp>
#include <functional>

namespace conway::ui {
struct CudaDidacticPrimitives {
    std::function<ftxui::Element(bool,std::size_t)> cell;
    std::function<ftxui::Element(std::string)> grid;
    bool colors{false};
};
// Const confirmed board + small cached detail. No stepping, copying Game, CUDA
// queries, or whole-board maps; only the visible rectangle is rendered.
ftxui::Element renderCudaDidactic(const Game&,const app::CudaDidacticFrame&,
    const std::string& gpu,const std::string& error,BoardViewport&,bool detailOnly,
    std::size_t detailOffset,bool followSelection,int width,int height,
    const CudaDidacticPrimitives&,bool showCoverage=false);
}

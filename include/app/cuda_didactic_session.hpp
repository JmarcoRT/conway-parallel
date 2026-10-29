#pragma once
#include "app/simulation.hpp"
#include "core/cell_probe.hpp"
#include <functional>

namespace conway::app {
struct CudaCellDetail {
    bool valid{false};
    core::CellEvaluation evaluation{};
};
// Checks the selected center BEFORE invoking reader; reads at most nine valid
// cells. The tiny CPU stencil uses the SAME evaluator as the CUDA kernel.
CudaCellDetail evaluateCudaCell(std::size_t width,std::size_t height,
                               std::size_t column,std::size_t row,
                               const std::function<bool(std::size_t,std::size_t)>& reader);
struct CudaDidacticFrame {
    core::CudaLaunchGeometry geometry{};
    std::size_t width{},height{},generation{},alive{};
    std::size_t blockX{},blockY{},threadX{},threadY{},column{},row{};
    CudaCellDetail cell;
    bool selectionAdjusted{false};
};
// Small presentation model, no board ownership or GPU handles. The UI owns the
// existing session; renders consume frame() and its confirmed Game by const ref.
class CudaDidacticController {
public:
    bool initialized() const noexcept {return initialized_;}
    const CudaDidacticFrame& frame() const noexcept {return frame_;}
    std::size_t revision() const noexcept {return revision_;}
    void refresh(const Game&); // Explicit entry/state/configuration change only.
    void enter(SimulationSession&);
    void selectBlock(std::size_t x,std::size_t y,const Game&);
    void selectThread(std::size_t x,std::size_t y,const Game&);
    void moveBlock(int direction,const Game&);
    void moveThread(int dx,int dy,const Game&);
    void nextThread(const Game&);
    bool advance(SimulationSession&);
    bool reset(SimulationSession&);
    bool recover(SimulationSession&);
private:
    void updateDetail(const Game&);
    CudaDidacticFrame frame_{};
    bool initialized_{false};
    std::size_t revision_{};
};
}

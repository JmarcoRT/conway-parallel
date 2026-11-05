#include "app/cuda_didactic_session.hpp"
#include "core/bs23.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace conway::app {
CudaCellDetail evaluateCudaCell(std::size_t width,std::size_t height,
                               std::size_t column,std::size_t row,
                               const std::function<bool(std::size_t,std::size_t)>& reader) {
    if(column>=width || row>=height)return {};
    std::array<std::uint8_t,9> stencil{};
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        if((dy<0 && row==0)||(dx<0 && column==0))continue;
        const auto r=dy<0?row-1:dy>0?row+1:row;
        const auto c=dx<0?column-1:dx>0?column+1:column;
        if(r<height && c<width)stencil[(dy+1)*3+dx+1]=reader(r,c)?1:0;
    }
    CudaCellDetail detail;detail.valid=true;
    core::next_cell_state(stencil.data(),3,1,1,detail.evaluation);
    return detail;
}
void CudaDidacticController::updateDetail(const Game& game) {
    const auto b=frame_.geometry.block;
    frame_.column=frame_.blockX*b.x+frame_.threadX;
    frame_.row=frame_.blockY*b.y+frame_.threadY;
    frame_.cell=evaluateCudaCell(game.width(),game.height(),frame_.column,frame_.row,
        [&game](std::size_t r,std::size_t c){return game.isAlive(r,c);});
    ++revision_;
}
void CudaDidacticController::refresh(const Game& game) {
    auto column=initialized_?frame_.column:0, row=initialized_?frame_.row:0;
    const auto geometry=core::planCudaLaunch(game.width(),game.height(),game.cudaBlockSize());
    const bool adjusted=column>=geometry.gridX*geometry.block.x || row>=geometry.gridY*geometry.block.y;
    if(adjusted) {column=std::min(column,game.width()-1);row=std::min(row,game.height()-1);}
    frame_.geometry=geometry;frame_.width=game.width();frame_.height=game.height();
    frame_.generation=game.generation();frame_.alive=game.aliveCount();
    frame_.blockX=column/geometry.block.x;frame_.blockY=row/geometry.block.y;
    frame_.threadX=column%geometry.block.x;frame_.threadY=row%geometry.block.y;
    frame_.selectionAdjusted=adjusted;initialized_=true;updateDetail(game);
}
void CudaDidacticController::enter(SimulationSession& session) {
    if(session.engine()!=Engine::CUDA)throw std::logic_error("Didáctico CUDA requiere una sesión CUDA");
    session.pause();refresh(session.game());
}
void CudaDidacticController::selectBlock(std::size_t x,std::size_t y,const Game& game) {
    if(!initialized_ || x>=frame_.geometry.gridX || y>=frame_.geometry.gridY)
        throw std::out_of_range("blockIdx fuera de la cuadrícula");
    frame_.blockX=x;frame_.blockY=y;frame_.selectionAdjusted=false;updateDetail(game);
}
void CudaDidacticController::selectThread(std::size_t x,std::size_t y,const Game& game) {
    if(!initialized_ || x>=frame_.geometry.block.x || y>=frame_.geometry.block.y)
        throw std::out_of_range("threadIdx fuera del bloque");
    frame_.threadX=x;frame_.threadY=y;frame_.selectionAdjusted=false;updateDetail(game);
}
void CudaDidacticController::moveBlock(int direction,const Game& game) {
    if(!initialized_)throw std::logic_error("Exploración no inicializada");
    const auto index=frame_.blockY*frame_.geometry.gridX+frame_.blockX;
    const auto total=frame_.geometry.gridX*frame_.geometry.gridY;
    const auto next=direction<0?(index?index-1:0):std::min(index+1,total-1);
    selectBlock(next%frame_.geometry.gridX,next/frame_.geometry.gridX,game);
}
void CudaDidacticController::moveThread(int dx,int dy,const Game& game) {
    if(!initialized_)throw std::logic_error("Exploración no inicializada");
    const auto move=[](std::size_t value,int delta,std::size_t limit) {
        return delta<0?(value?value-1:0):delta>0?std::min(value+1,limit-1):value;
    };
    selectThread(move(frame_.threadX,dx,frame_.geometry.block.x),move(frame_.threadY,dy,frame_.geometry.block.y),game);
}
void CudaDidacticController::nextThread(const Game& game) {
    if(!initialized_)throw std::logic_error("Exploración no inicializada");
    const auto index=frame_.threadY*frame_.geometry.block.x+frame_.threadX;
    const auto next=std::min(index+1,frame_.geometry.threadsPerBlock-1);
    selectThread(next%frame_.geometry.block.x,next/frame_.geometry.block.x,game);
}
bool CudaDidacticController::advance(SimulationSession& session) {
    session.pause();
    if(session.engine()!=Engine::CUDA || session.game().engine()!=Engine::CUDA || session.busy() || session.closed() || !session.error().empty())return false;
    session.step();
    if(!session.error().empty())return false;
    refresh(session.game());return true;
}
bool CudaDidacticController::reset(SimulationSession& session) {
    if(session.engine()!=Engine::CUDA)return false;
    session.pause();session.reset();if(!session.error().empty() || session.busy())return false;
    initialized_=false;refresh(session.game());return true;
}
bool CudaDidacticController::recover(SimulationSession& session) {
    if(session.engine()!=Engine::CUDA)return false;
    session.pause();session.recover();if(!session.error().empty() || session.busy())return false;
    refresh(session.game());return true;
}
}

#include "app/cuda_didactic_session.hpp"
#include "app/cuda_info.hpp"
#include "game_test_access.hpp"
#include "ui/cuda_didactic_view.hpp"
#include <ftxui/screen/screen.hpp>
#include <iostream>
#include <stdexcept>
using namespace conway;
using namespace conway::app;
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
int main() {
 try {
    auto info=inspectCuda(35,19);
    if(!info.available) {
        if(info.diagnostic.find("GPU ausente")!=std::string::npos)return 77;
        if(info.diagnostic.find("build sin CUDA")!=std::string::npos) {std::cout<<"OFF: integration GPU intentionally unavailable\n";return 77;}
        throw std::runtime_error(info.diagnostic);
    }
    InputData input{35,19,std::vector<std::uint8_t>(35*19)};
    for(std::size_t i=0;i<input.cells.size();++i)input.cells[i]=(i*17+i/35)%7<3;
    SimulationSession session(input,Engine::CUDA);Game cpu(input);CudaDidacticController model;
    session.toggleRunning();model.enter(session);require(!session.isRunning(),"Normal playback was not paused");
    for(auto block:{core::kCudaBlock8x8,core::kCudaBlock16x16,core::kCudaBlock32x8}) {
        const auto saved=session.game();session.configure(Engine::CUDA,1,block);model.refresh(session.game());
        require(detail::sameGameBuffers(saved,session.game()),"Preset changed buffers/counters");
        const auto geom=core::planCudaLaunch(35,19,block);std::size_t inside=0,outside=0;
        for(std::size_t by=0;by<geom.gridY;++by)for(std::size_t bx=0;bx<geom.gridX;++bx) {
            model.selectBlock(bx,by,session.game());
            for(unsigned y=0;y<block.y;++y)for(unsigned x=0;x<block.x;++x) {
                model.selectThread(x,y,session.game());const auto& f=model.frame();
                require(f.column==bx*block.x+x && f.row==by*block.y+y,"Preset mapping");
                if(f.cell.valid)++inside;else ++outside;
            }
        }
        require(inside==665 && outside==geom.outsidePositions,"Preset valid/outside counts");
        require(detail::sameGameBuffers(saved,session.game()),"Exploration mutated confirmed state");
        model.selectBlock(0,0,session.game());model.selectThread(1,1,session.game());
        const auto prediction=model.frame().cell.evaluation.after;
        require(model.advance(session),"CUDA generation failed");cpu.step();
        require(session.game().isAlive(1,1)==bool(prediction) && detail::sameGameBuffers(session.game(),cpu),"CPU prediction/real CUDA mismatch");
        const auto revision=model.revision();ui::BoardViewport viewport;
        ui::CudaDidacticPrimitives style{
            [](bool alive,std::size_t width){return ftxui::text(std::string(width,alive?'#':'.'));},
            [](std::string line){return ftxui::text(std::move(line));},false};
        for(auto [w,h]:{std::pair{120,32},{40,10}}) {
            auto screen=ftxui::Screen::Create(ftxui::Dimension::Fixed(w),ftxui::Dimension::Fixed(h));
            for(int repeat=0;repeat<3;++repeat) {
                ftxui::Render(screen,ui::renderCudaDidactic(session.game(),model.frame(),info.gpu,"",viewport,false,0,true,w,h,style));
                require(model.revision()==revision,"Rendering mutated model/detail cache");
            }
        }
    }
    model.selectBlock(1,1,session.game());model.selectThread(2,2,session.game());auto c=model.frame().column,r=model.frame().row;
    session.configure(Engine::CUDA,1,core::kCudaBlock16x16);model.refresh(session.game());
    require(model.frame().column==c && model.frame().row==r,"Valid global selection lost at preset change");
    model.selectBlock(2,1,session.game());model.selectThread(15,15,session.game()); // 47,31
    session.configure(Engine::CUDA,1,core::kCudaBlock32x8);model.refresh(session.game());
    require(model.frame().selectionAdjusted && model.frame().column==34 && model.frame().row==18,"Exterior remap not clamped to last cell");
    auto saved=session.game();bool rejected=false;
    try{session.configure(Engine::CUDA,1,{32,32});}catch(const std::invalid_argument&){rejected=true;}
    require(rejected && detail::sameGameBuffers(saved,session.game()),"Rejected preset modified state");
    require(model.reset(session) && model.frame().generation==0 && model.frame().column==0 && model.frame().row==0,"Reset state/selection");
    require(session.game().cudaBlockSize()==core::kCudaBlock32x8,"Reset lost preset");
    std::cout<<"PASS CUDA didactic integration: all presets/35x19, real advances, full buffers, remap, reset, const rendering\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

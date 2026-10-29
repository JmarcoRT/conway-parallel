#include "app/cuda_didactic_session.hpp"
#include "game_test_access.hpp"
#include <iostream>
#include <stdexcept>
using namespace conway;
using namespace conway::app;
void require(bool ok,const char* msg) {if(!ok)throw std::runtime_error(msg);}
int main() {
    try {
        static_assert(sizeof(CudaDidacticFrame)<512,"Frame must not contain full board maps");
        InputData input{35,19,std::vector<std::uint8_t>(35*19,0)};
        for(auto [r,c]:{std::pair{5,15},{5,16},{5,17},{17,33},{17,34},{18,34}})input.cells[r*35+c]=1;
        Game game(input),saved(game);CudaDidacticController model;model.refresh(game);
        require(model.frame().geometry.gridX==3 && model.frame().geometry.gridY==2,"35x19 grid");
        require(model.frame().geometry.outsidePositions==871,"35x19 outside positions");
        model.selectBlock(1,0,game);model.selectThread(0,5,game);
        require(model.frame().column==16 && model.frame().cell.evaluation.neighborCount==2 && model.frame().cell.evaluation.after==1,
                "Neighbors across a block boundary were lost");
        model.selectBlock(2,1,game);model.selectThread(2,2,game);
        require(model.frame().cell.valid && model.frame().column==34 && model.frame().row==18 &&
                model.frame().cell.evaluation.neighborCount==2,"Last valid cell/stencil");
        for(auto [x,y]:{std::pair{3u,2u},{2u,3u},{15u,15u}}) {
            model.selectThread(x,y,game);require(!model.frame().cell.valid,"Exterior thread evaluated a cell");
        }
        std::size_t reads=0;
        for(auto [col,row]:{std::pair<std::size_t,std::size_t>{35,18},{34,19},{47,31}}) {
            auto result=evaluateCudaCell(35,19,col,row,[&](auto,auto){++reads;throw std::runtime_error("Exterior read");return false;});
            require(!result.valid && reads==0,"Exterior position must not invoke reader");
        }
        evaluateCudaCell(35,19,0,0,[&](auto r,auto c){require(r<19&&c<35,"Invalid neighbor read");++reads;return game.isAlive(r,c);});
        require(reads==4,"Corner reads only center and three valid neighbors");
        for(unsigned mask=0;mask<512;++mask) {
            auto value=evaluateCudaCell(3,3,1,1,[&](std::size_t r,std::size_t c){return (mask>>(r*3+c))&1;});
            const auto before=(mask>>4)&1u;
            unsigned neighbors=0;for(unsigned i=0;i<9;++i)if(i!=4)neighbors+=(mask>>i)&1u;
            require(value.valid && value.evaluation.neighborCount==neighbors &&
                    value.evaluation.after==(neighbors==3 || (before && neighbors==2)),"B3/S23 CPU prediction");
        }
        for(std::size_t by=0;by<2;++by)for(std::size_t bx=0;bx<3;++bx) {
            model.selectBlock(bx,by,game);
            for(std::size_t ty=0;ty<16;++ty)for(std::size_t tx=0;tx<16;++tx) {
                model.selectThread(tx,ty,game);const auto& f=model.frame();
                require(f.column==bx*16+tx && f.row==by*16+ty && f.cell.valid==(f.column<35 && f.row<19),"Coordinate mapping");
            }
        }
        const auto revision=model.revision();const auto* frame=&model.frame();
        for(int i=0;i<100;++i)require(&model.frame()==frame && model.revision()==revision,"Render getter recomputed cache");
        require(detail::sameGameBuffers(game,saved),"Exploration mutated buffers/counters");
        bool invalid=false;try {model.selectThread(16,0,game);}catch(const std::out_of_range&){invalid=true;}
        require(invalid && detail::sameGameBuffers(game,saved),"Invalid selection changed board");
        std::cout<<"PASS CPU model: 35x19, exterior zero reads, cross-block neighbors, 512 rules, stable buffers/cache\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

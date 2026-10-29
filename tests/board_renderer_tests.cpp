#include "ui/board_renderer.hpp"
#include "ui/cuda_didactic_view.hpp"
#include <ftxui/screen/screen.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace conway;
using namespace ftxui;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
Element originalCell(bool alive,std::size_t width,bool colors) {
    auto e=text(std::string(width,' '));
    if(!colors)return alive?e|inverted:e;
    return e|bgcolor(alive?Color::RGB(98,232,137):Color::RGB(31,42,58));
}
Element grid(std::string value,bool colors) {
    auto e=text(std::move(value));return colors?e|color(Color::RGB(88,103,111)):e;
}
std::tuple<int,int,int,int> bounds(const Screen& screen) {
    int x0=screen.dimx(),x1=-1,y0=screen.dimy(),y1=-1;
    for(int y=0;y<screen.dimy();++y)for(int x=0;x<screen.dimx();++x) {
        const auto& c=screen.PixelAt(x,y).character;
        if(c=="┌"||c=="┐"||c=="└"||c=="┘") {x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}
    }
    return {x0,x1,y0,y1};
}
int main(int argc,char** argv) {
 try {
    for(bool colors:{false,true})for(std::size_t width:{2,3,4,8})for(bool alive:{false,true}) {
        auto selected=Screen::Create(Dimension::Fixed(width),Dimension::Fixed(1));
        auto neighbor=Screen::Create(Dimension::Fixed(width),Dimension::Fixed(1));
        Render(selected,ui::markedBoardCell(alive,width,ui::BoardCellMark::Selected,colors));
        Render(neighbor,ui::markedBoardCell(alive,width,ui::BoardCellMark::Neighbor,colors));
        const auto left=(width-2)/2;
        require(selected.PixelAt(left,0).character=="@" && selected.PixelAt(left+1,0).character==(alive?"1":"0"),"Selected token lost state/width");
        require(neighbor.PixelAt(left,0).character==":" && neighbor.PixelAt(left+1,0).character==(alive?"1":"0"),"Neighbor token lost state/width");
        require(selected.PixelAt(left,0).bold && neighbor.PixelAt(left,0).underlined,"Monochrome attributes missing");
        if(colors)require(selected.PixelAt(left,0).background_color!=neighbor.PixelAt(left,0).background_color,"Selection/neighbor colors conflated");
        for(std::size_t x=0;x<width;++x)if(x<left || x>=left+2)
            require(selected.PixelAt(x,0).character==" " && neighbor.PixelAt(x,0).character==" ","Markers deform cell padding");
    }
    auto real=ui::boardBoundary(16,35,0,35,16);
    auto edge=ui::boardBoundary(35,35,0,35,16);
    auto cut=ui::boardBoundary(5,35,5,15,16);
    auto both=ui::boardBoundary(16,35,16,26,16);
    require(real.realBlock && !real.boardEdge && !real.viewportCut,"Real block edge");
    require(edge.boardEdge && !edge.realBlock && !edge.viewportCut,"Partial board edge mistaken for block");
    require(cut.viewportCut && !cut.boardEdge && !cut.realBlock,"Viewport edge mistaken for board/block");
    require(both.viewportCut && both.realBlock,"Coincident boundaries lost provenance");
    for(auto [w,h]:{std::pair{5u,5u},{35u,19u}})for(bool colors:{false,true}) {
        InputData input{w,h,std::vector<std::uint8_t>(w*h,0)};
        input.cells[2*w+2]=input.cells[w+2]=input.cells[3*w+2]=1;
        Game game(input);app::CudaDidacticController model;model.refresh(game);
        ui::CudaDidacticPrimitives style{[&](bool a,std::size_t cw){return originalCell(a,cw,colors);},[&](std::string s){return grid(std::move(s),colors);},colors};
        for(auto preset:{core::kCudaBlock8x8,core::kCudaBlock16x16,core::kCudaBlock32x8})
        for(auto mode:{0,1,2})for(auto [width,height]:{std::pair{180,55},{120,32},{40,10}}) {
            auto frame=model.frame();
            frame.geometry=core::planCudaLaunch(w,h,preset);
            frame.blockX=mode==2?frame.geometry.gridX-1:0;
            frame.blockY=mode==2?frame.geometry.gridY-1:0;
            frame.threadX=mode==0?2:mode==1?0:preset.x-1;
            frame.threadY=mode==0?2:mode==1?0:preset.y-1;
            frame.column=frame.blockX*preset.x+frame.threadX;
            frame.row=frame.blockY*preset.y+frame.threadY;
            frame.cell=app::evaluateCudaCell(w,h,frame.column,frame.row,[&](auto r,auto c){return game.isAlive(r,c);});
            ui::BoardViewport viewport;
            auto plain=Screen::Create(Dimension::Fixed(width),Dimension::Fixed(height));
            Render(plain,ui::renderCudaDidactic(game,frame,"RTX 3050","",viewport,false,0,true,width,height,style,false));
            require(plain.ToString().find("×")==std::string::npos,"Real board expanded with exterior cells");
            auto box=bounds(plain);auto [x0,x1,y0,y1]=box;
            require(x1>=x0 && y1>=y0,"Board vanished in minimum terminal");
            if(mode==0 && width==180) {
                const auto& contour=plain.PixelAt(x0+1,y0);
                if(colors)require(contour.foreground_color==Color::RGB(83,191,208),"Base grid style hides selected contour");
                else require(contour.inverted && contour.bold,"Selected contour missing without color");
            }
            if(w==5 && width==120)require(x1-x0+1==46 && y1-y0+1==11,"5x5 lost original adaptive cell size");
            auto oldViewport=viewport;
            auto coverage=Screen::Create(Dimension::Fixed(width),Dimension::Fixed(height));
            Render(coverage,ui::renderCudaDidactic(game,frame,"RTX 3050","",viewport,false,0,true,width,height,style,true));
            require(bounds(coverage)==box && oldViewport.row==viewport.row && oldViewport.column==viewport.column,"A moves/resizes the board or viewport");
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
                const auto& a=plain.PixelAt(x,y);const auto& b=coverage.PixelAt(x,y);
                require(a.character==b.character && a.background_color==b.background_color && a.foreground_color==b.foreground_color,"Coverage changes real board pixels");
            }
            if(argc>1) {
                std::string tag=std::string(argv[1])+"/after-"+std::to_string(preset.x)+"x"+std::to_string(preset.y)+"-"+std::to_string(w)+"-"+std::to_string(width)+"-"+std::to_string(mode)+(colors?"-color":"-mono");
                std::ofstream(tag+".ansi")<<plain.ToString();std::ofstream(tag+"-coverage.ansi")<<coverage.ToString();
            }
        }
    }
    std::cout<<"PASS common board geometry, optional marks, boundary provenance, logical extents and fixed A coverage layout\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

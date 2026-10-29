#include "ui/cuda_didactic_view.hpp"
#include "ui/board_renderer.hpp"
#include <algorithm>
#include <sstream>
#include <ftxui/screen/string.hpp>

namespace conway::ui {
using namespace ftxui;
namespace {
std::string pair(std::size_t x,std::size_t y) {return "("+std::to_string(x)+","+std::to_string(y)+")";}
std::vector<std::string> wrap(const std::string& text,int width) {
    std::istringstream input(text);std::string word,line;std::vector<std::string> lines;
    while(input>>word) {
        if(!line.empty() && string_width(line+" "+word)>width) {lines.push_back(line);line.clear();}
        if(!line.empty())line+=' ';
        line+=word;
    }
    if(!line.empty())lines.push_back(line);return lines;
}
void append(Elements& out,const std::string& value,int width) {
    for(const auto& line:wrap(value,std::max(1,width)))out.push_back(text(line));
}
bool near(std::size_t a,std::size_t b) {return a>b?a-b<=1:b-a<=1;}
Element board(const Game& game,const app::CudaDidacticFrame& f,BoardViewport& viewport,
              int width,int height,bool follow,const CudaDidacticPrimitives& primitives) {
    if(width<5 || height<3)return text("Amplía la terminal o V detalle");
    auto geometry=computeBoardGeometry(width,height,game.width(),game.height());
    const bool label=!geometry.full_board_shown && height>=6;
    if(label)geometry=computeBoardGeometry(width,height-1,game.width(),game.height());
    // An exterior thread is not a cell: don't move the real-board camera to it.
    if(follow && f.cell.valid && geometry.visible_rows && geometry.visible_cols) {
        if(f.row<viewport.row)viewport.row=f.row;
        if(f.row>=viewport.row+geometry.visible_rows)viewport.row=f.row-geometry.visible_rows+1;
        if(f.column<viewport.column)viewport.column=f.column;
        if(f.column>=viewport.column+geometry.visible_cols)viewport.column=f.column-geometry.visible_cols+1;
    }
    viewport.clamp(game.height(),game.width(),geometry.visible_rows,geometry.visible_cols);
    geometry.row_offset=viewport.row;geometry.col_offset=viewport.column;
    BoardPrimitives paint{
        [&](std::size_t r,std::size_t c,std::size_t cw){return primitives.cell(game.isAlive(r,c),cw);},primitives.grid};
    BoardDecorations decorations{f.geometry.block.x,f.geometry.block.y,f.blockX,f.blockY,primitives.colors,
        [&](std::size_t r,std::size_t c) {
            if(!f.cell.valid)return BoardCellMark::None;
            if(r==f.row && c==f.column)return BoardCellMark::Selected;
            return near(r,f.row) && near(c,f.column)?BoardCellMark::Neighbor:BoardCellMark::None;
        },[&](std::size_t r,std::size_t c){return game.isAlive(r,c);}};
    std::string caption;
    if(label)caption="base 0: f"+std::to_string(viewport.row)+"–"+std::to_string(viewport.row+geometry.visible_rows-1)+
                     " c"+std::to_string(viewport.column)+"–"+std::to_string(viewport.column+geometry.visible_cols-1);
    return renderBoardGrid(geometry,game.width(),game.height(),paint,&decorations,std::move(caption));
}
Element legend(const app::CudaDidacticFrame& f,int width,bool colors,bool coverage) {
    Elements items{
        markedBoardCell(true,2,BoardCellMark::Selected,colors),text("/"),markedBoardCell(false,2,BoardCellMark::Selected,colors),text(width<64?" sel ":" selección  "),
        markedBoardCell(true,2,BoardCellMark::Neighbor,colors),text("/"),markedBoardCell(false,2,BoardCellMark::Neighbor,colors),text(width<64?" vec ":" vecinos  "),
        (colors?text("━")|bold|color(Color::RGB(83,191,208)):text("━")|bold|inverted),text(width<64?"B ":" bloque  "),
        text("─"),text(width<64?"T ":" fin tablero  "),text("┄"),text(width<64?"V":" recorte vista")};
    if(coverage)items.push_back(text(width<64?" A:+"+std::to_string(f.geometry.outsidePositions):
        " C"+std::to_string(f.geometry.gridX*f.geometry.block.x)+"x"+
        std::to_string(f.geometry.gridY*f.geometry.block.y)+" +"+std::to_string(f.geometry.outsidePositions)));
    return hbox(std::move(items))|size(HEIGHT,EQUAL,1);
}
Elements details(const app::CudaDidacticFrame& f,const std::string& error,int width,bool coverage) {
    Elements out;
    if(!error.empty())append(out,error,width);
    if(coverage) {
        append(out,"Cobertura A (esquema, no células): real "+std::to_string(f.width)+"x"+std::to_string(f.height)+
            ", lanzamiento "+std::to_string(f.geometry.gridX*f.geometry.block.x)+"x"+std::to_string(f.geometry.gridY*f.geometry.block.y)+
            ", exteriores "+std::to_string(f.geometry.outsidePositions),width);
        const auto x0=f.blockX*f.geometry.block.x,y0=f.blockY*f.geometry.block.y;
        const auto validX=x0<f.width?std::min<std::size_t>(f.geometry.block.x,f.width-x0):0;
        const auto validY=y0<f.height?std::min<std::size_t>(f.geometry.block.y,f.height-y0):0;
        append(out,"Bloque seleccionado: parte válida "+std::to_string(validX)+"x"+std::to_string(validY)+
            "; posiciones exteriores "+std::to_string(f.geometry.threadsPerBlock-validX*validY),width);
        if(f.geometry.gridX<=4 && f.geometry.gridY<=3) {
            append(out,"Mapa de bloques (* seleccionado):",width);
            for(std::size_t y=0;y<f.geometry.gridY;++y) {
                std::string row;for(std::size_t x=0;x<f.geometry.gridX;++x)row+=(x==f.blockX&&y==f.blockY)?"[*]":"[ ]";
                out.push_back(text(row));
            }
        }
    }
    append(out,"Las divisiones entre bloques no son fronteras muertas: los vecinos pueden cruzarlas.",width);
    append(out,"blockIdx="+pair(f.blockX,f.blockY)+" threadIdx="+pair(f.threadX,f.threadY)+" (base cero)",width);
    append(out,"columna = "+std::to_string(f.blockX)+" * "+std::to_string(f.geometry.block.x)+" + "+std::to_string(f.threadX)+" = "+std::to_string(f.column),width);
    append(out,"fila = "+std::to_string(f.blockY)+" * "+std::to_string(f.geometry.block.y)+" + "+std::to_string(f.threadY)+" = "+std::to_string(f.row),width);
    if(f.cell.valid) {
        const auto& e=f.cell.evaluation;
        append(out,"Predicción CPU con regla compartida B3/S23 sobre G"+std::to_string(f.generation)+
            ": actual "+std::to_string(e.before)+", vecinos vivos "+std::to_string(e.neighborCount)+", resultado "+std::to_string(e.after),width);
        static constexpr const char* names[]={"NW","N","NE","W","E","SW","S","SE"};
        std::string neighbors;
        for(unsigned i=0;i<8;++i)neighbors+=std::string(names[i])+"="+std::to_string(e.neighbors[i])+" ";
        append(out,neighbors,width);
        append(out,"B3: nace con 3; S23: sobrevive con 2 o 3. Borde global exterior = 0.",width);
    } else {
        append(out,"Posición exterior: este hilo no procesa una célula. Sin lecturas de células ni evaluación B3/S23.",width);
        append(out,"En el kernel aporta cero a la reducción y participa en todas las barreras del bloque; no retorna antes de ellas.",width);
    }
    append(out,"G avanza una generación CUDA real; la predicción CPU no escribe NEXT ni avanza el tablero.",width);
    append(out,"@1/@0: selección viva/muerta; :1/:0: vecinos. Contorno cian: bloque seleccionado, sin rellenar sus células.",width);
    append(out,"Línea gruesa: límite real de bloque; fina: fin del tablero que corta bloque; discontinua: recorte del viewport, incluso si coincide con límite de bloque.",width);
    append(out,"A muestra cobertura en esquema compacto sin ampliar ni desplazar el tablero real. Si el hilo es exterior no se marca otra célula como sustituta.",width);
    if(f.selectionAdjusted)append(out,"Selección ajustada: la posición anterior no está en la nueva cobertura.",width);
    return out;
}
}
Element renderCudaDidactic(const Game& game,const app::CudaDidacticFrame& f,const std::string& gpu,
    const std::string& error,BoardViewport& viewport,bool detailOnly,std::size_t detailOffset,
    bool follow,int width,int height,const CudaDidacticPrimitives& primitives,bool coverage) {
    Elements header;
    append(header,"Exploración conceptual; no representa el orden de ejecución GPU",width);
    append(header,"GPU: "+gpu,width);
    append(header,"G"+std::to_string(f.generation)+" · B"+std::to_string(f.geometry.block.x)+"x"+std::to_string(f.geometry.block.y)+
                  " · grid "+std::to_string(f.geometry.gridX)+"x"+std::to_string(f.geometry.gridY)+" · real "+std::to_string(f.width)+"x"+std::to_string(f.height),width);
    header.push_back(legend(f,width,primitives.colors,coverage));
    const int remaining=std::max(1,height-static_cast<int>(header.size()));
    Element body;
    if(detailOnly) {
        auto all=details(f,error,width,coverage);Elements visible;
        const auto start=std::min(detailOffset,all.empty()?0:all.size()-1);
        for(std::size_t i=start;i<all.size() && visible.size()<static_cast<std::size_t>(remaining);++i)visible.push_back(all[i]);
        body=vbox(std::move(visible));
    } else if(width>=110 && remaining>=12) {
        const int left=width*2/3;
        auto info=details(f,error,width-left-1,coverage);
        body=hbox({board(game,f,viewport,left,remaining,follow,primitives)|size(WIDTH,EQUAL,left),
                   separator(),vbox(std::move(info))|yframe|flex});
    } else {
        Elements summary;
        append(summary,std::string(f.selectionAdjusted?"Ajuste: ":"")+"b"+pair(f.blockX,f.blockY)+" t"+pair(f.threadX,f.threadY)+" → c"+std::to_string(f.column)+" f"+std::to_string(f.row),width);
        if(!error.empty())append(summary,"Error CUDA · V diagnóstico · U recupera",width);
        else if(f.cell.valid)append(summary,"Pred. CPU B3/S23 compartida: "+std::to_string(f.cell.evaluation.before)+"/"+std::to_string(f.cell.evaluation.neighborCount)+"→"+std::to_string(f.cell.evaluation.after),width);
        else append(summary,"Exterior: sin célula · G=CUDA real",width);
        const int boardHeight=remaining-static_cast<int>(summary.size());
        summary.push_back(board(game,f,viewport,width,boardHeight,follow,primitives)|flex);
        body=vbox(std::move(summary));
    }
    header.push_back(std::move(body)|flex);return vbox(std::move(header));
}
}

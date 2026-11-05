#include "ui/cuda_didactic_view.hpp"
#include "ui/board_renderer.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <array>
#include <iterator>
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
constexpr std::array<std::array<int,3>,8> blockPalette{{
    {90,143,232},{232,158,90},{180,90,232},{232,216,90},
    {90,232,200},{232,90,158},{232,90,90},{138,106,232}
}};
const std::array<int,3>& blockRgb(std::size_t x,std::size_t y) {
    return blockPalette[(x+3*y)%blockPalette.size()];
}
Color blockInk(std::size_t x,std::size_t y) {
    const auto& rgb=blockRgb(x,y);return Color::RGB(rgb[0],rgb[1],rgb[2]);
}
Color blockShade(std::size_t x,std::size_t y) {
    const auto& rgb=blockRgb(x,y);return Color::RGB(18+rgb[0]/5,22+rgb[1]/5,28+rgb[2]/5);
}
bool near(std::size_t a,std::size_t b) {return a>b?a-b<=1:b-a<=1;}
Element board(const Game& game,const app::CudaDidacticFrame& f,BoardViewport& viewport,
              int width,int height,bool follow,const CudaDidacticPrimitives& primitives) {
    if(width<5 || height<3)return text("Amplía la terminal o V detalle");
    // A terminal character is usually about twice as tall as it is wide.
    // Two content columns keep cells compact, as in the didactic worker views.
    // Keep spare horizontal space around the board rather than widening cells.
    constexpr std::size_t maxCellChars=2;
    auto geometry=computeBoardGeometry(width,height,game.width(),game.height(),maxCellChars);
    const bool label=!geometry.full_board_shown && height>=6;
    if(label)geometry=computeBoardGeometry(width,height-1,game.width(),game.height(),maxCellChars);
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
        [&](std::size_t r,std::size_t c,std::size_t cw) {
            const bool alive=game.isAlive(r,c);
            if(primitives.colors && !alive)
                return text(std::string(cw,' '))|bgcolor(blockShade(c/f.geometry.block.x,r/f.geometry.block.y));
            return primitives.cell(alive,cw);
        },primitives.grid};
    BoardDecorations decorations{f.geometry.block.x,f.geometry.block.y,f.blockX,f.blockY,primitives.colors,
        [&](std::size_t r,std::size_t c) {
            if(!f.cell.valid)return BoardCellMark::None;
            if(r==f.row && c==f.column)return BoardCellMark::Selected;
            return near(r,f.row) && near(c,f.column)?BoardCellMark::Neighbor:BoardCellMark::None;
        },[&](std::size_t r,std::size_t c){return game.isAlive(r,c);},
        [&](std::size_t r,std::size_t c){return blockInk(c/f.geometry.block.x,r/f.geometry.block.y);}};
    std::string caption;
    if(label)caption="base 0: f"+std::to_string(viewport.row)+"–"+std::to_string(viewport.row+geometry.visible_rows-1)+
                     " c"+std::to_string(viewport.column)+"–"+std::to_string(viewport.column+geometry.visible_cols-1);
    return renderBoardGrid(geometry,game.width(),game.height(),paint,&decorations,std::move(caption));
}
Element legend(const app::CudaDidacticFrame& f,int width,bool colors,bool coverage) {
    Elements items{
        text("║")|bold,text(width<64?" sel ":" selección  "),
        text("┃")|bold,text(width<64?" vec ":" vecinos  "),
        (colors?text("━")|bold|color(Color::RGB(83,191,208)):text("━")|bold|inverted),text(width<64?"B ":" bloque  "),
        text("─"),text(width<64?"T ":" fin tablero  "),text("┄"),text(width<64?"V":" recorte vista")};
    if(coverage)items.push_back(text(width<64?" A:+"+std::to_string(f.geometry.outsidePositions):
        " C"+std::to_string(f.geometry.gridX*f.geometry.block.x)+"x"+
        std::to_string(f.geometry.gridY*f.geometry.block.y)+" +"+std::to_string(f.geometry.outsidePositions)));
    return hbox(std::move(items))|size(HEIGHT,EQUAL,1);
}
void heading(Elements& out,const std::string& value,int width,bool colors) {
    const auto first=out.size();append(out,value,width);
    for(auto i=first;i<out.size();++i) {
        out[i]=out[i]|bold;
        if(colors)out[i]=out[i]|color(Color::RGB(83,191,208));
    }
}
Elements blockTable(const app::CudaDidacticFrame& f,int width,int height,bool colors) {
    Elements out;
    const auto total=f.geometry.gridX*f.geometry.gridY;
    if(!total) return out;
    heading(out,"BLOQUES CUDA",width,colors);
    const int blockWidth=std::max(6,string_width(pair(f.geometry.gridX-1,f.geometry.gridY-1))+2);
    const int countWidth=std::max(5,static_cast<int>(std::to_string(f.geometry.threadsPerBlock).size()));
    const int cellWidth=std::max(6,countWidth);
    const int tableWidth=blockWidth+countWidth+cellWidth+4;
    const auto pad=[](const std::string& value,int columns,bool right) {
        const std::string spaces(static_cast<std::size_t>(std::max(0,columns-string_width(value))),' ');
        return right?spaces+value:value+spaces;
    };
    auto header=text(pad("Bloque",blockWidth,false)+"  "+pad("Hilos",countWidth,true)+"  "+pad("Celdas",cellWidth,true))|bold;
    if(colors) header=header|color(Color::RGB(83,191,208));
    out.push_back(std::move(header));
    out.push_back(separator()|dim|size(WIDTH,EQUAL,tableWidth));
    const auto capacity=static_cast<std::size_t>(std::clamp(height-4,1,9));
    const auto selected=std::min(total-1,f.blockY*f.geometry.gridX+f.blockX);
    const auto first=selected/capacity*capacity;
    const auto last=std::min(total,first+capacity);
    for(auto index=first;index<last;++index) {
        const auto bx=index%f.geometry.gridX,by=index/f.geometry.gridX;
        const auto x0=bx*f.geometry.block.x,y0=by*f.geometry.block.y;
        const auto nx=x0<f.width?std::min<std::size_t>(f.geometry.block.x,f.width-x0):0;
        const auto ny=y0<f.height?std::min<std::size_t>(f.geometry.block.y,f.height-y0):0;
        const auto cells=nx*ny;
        const bool current=index==selected;
        auto label=text(pad(std::string(current?"> ":colors?"▌ ":"  ")+pair(bx,by),blockWidth,false));
        if(colors) label=label|color(blockInk(bx,by));
        if(current) label=label|bold;
        auto row=hbox({std::move(label),text("  "+pad(std::to_string(f.geometry.threadsPerBlock),countWidth,true)+
            "  "+pad(std::to_string(cells),cellWidth,true))});
        if(current) row=colors?row|bgcolor(Color::RGB(25,33,43)):row|bold|inverted;
        out.push_back(std::move(row));
    }
    if(first || last<total) append(out,"Bloques "+std::to_string(first+1)+" a "+std::to_string(last)+" de "+std::to_string(total),width);
    return out;
}
Elements threadCalculation(const app::CudaDidacticFrame& f,const std::string& error,int width,bool colors,bool coverage) {
    Elements out;
    if(!error.empty()) {heading(out,"CUDA DETENIDA",width,colors);append(out,"V: ver diagnóstico. U: recuperar.",width);out.push_back(text(" "));}
    heading(out,"HILO SELECCIONADO",width,colors);
    append(out,"Bloque: "+pair(f.blockX,f.blockY)+"   Hilo: "+pair(f.threadX,f.threadY),width);
    append(out,"Celda: fila "+std::to_string(f.row)+", columna "+std::to_string(f.column),width);
    out.push_back(text(" "));
    if(f.cell.valid) {
        const auto& e=f.cell.evaluation;
        heading(out,"LEE SU CELDA Y VECINOS",width,colors);
        const std::array<int,9> values{e.neighbors[0],e.neighbors[1],e.neighbors[2],e.neighbors[3],e.before,e.neighbors[4],e.neighbors[5],e.neighbors[6],e.neighbors[7]};
        bool exterior=false;
        for(int y=0;y<3;++y) {
            Elements cells;
            for(int x=0;x<3;++x) {
                const bool selected=x==1 && y==1;
                const bool outside=(x==0 && f.column==0)||(x==2 && f.column+1>=f.width)||
                    (y==0 && f.row==0)||(y==2 && f.row+1>=f.height);
                exterior=exterior || outside;
                const auto v=std::to_string(values[y*3+x]);
                auto cell=text(outside?" - ":selected?"["+v+"]":" "+v+" ");
                if(colors) cell=cell|color(selected?Color::RGB(255,209,102):outside?Color::RGB(150,160,170):
                    values[y*3+x]?Color::RGB(98,232,137):Color::RGB(180,190,200));
                if(selected) cell=cell|bold;
                cells.push_back(std::move(cell));
            }
            out.push_back(hbox(std::move(cells)));
        }
        if(exterior) append(out,"- Exterior del tablero: valor 0",width);
        std::string sum;
        for(const auto value:e.neighbors) {if(!sum.empty())sum+='+';sum+=std::to_string(value);}
        append(out,"Vecinos vivos: "+sum+" = "+std::to_string(e.neighborCount),width);
        out.push_back(text(" "));
        heading(out,"APLICA LA REGLA B3/S23",width,colors);
        const auto count=std::to_string(e.neighborCount)+(e.neighborCount==1?" vecino":" vecinos");
        std::string reason;
        if(!e.before) reason="Muerta con "+count+(e.after?": nace.":": sigue muerta.");
        else if(e.after) reason="Viva con "+count+": sobrevive.";
        else reason="Viva con "+count+(e.neighborCount<2?": muere por soledad.":": muere por exceso.");
        append(out,reason,width);
        auto result=text(std::string("Siguiente estado: ")+(e.after?"viva":"muerta"))|bold;
        if(colors) result=result|color(e.after?Color::RGB(98,232,137):Color::RGB(224,185,112));
        out.push_back(std::move(result));
        append(out,"Predicción CPU para G"+std::to_string(f.generation+1),width);out.back()=out.back()|dim;
    } else {
        heading(out,"POSICIÓN EXTERIOR",width,colors);
        append(out,"Este hilo no actualiza ninguna celda.",width);
        append(out,"Aporta 0 al conteo de vivas del bloque.",width);
        append(out,"La sincronización se explica en V.",width);
    }
    if(f.selectionAdjusted) {out.push_back(text(" "));append(out,"Selección ajustada a la nueva cobertura.",width);}
    if(coverage) {out.push_back(text(" "));append(out,"Cobertura A: "+std::to_string(f.geometry.outsidePositions)+" posiciones exteriores.",width);}
    out.push_back(text(" "));
    append(out,"V: ver explicación y controles",width);
    return out;
}
Elements selectionSummary(const app::CudaDidacticFrame& f,const std::string& error,int width,int height,bool colors,bool coverage) {
    auto calculation=threadCalculation(f,error,width,colors,coverage);
    auto out=blockTable(f,width,std::max(5,height-static_cast<int>(calculation.size())-1),colors);
    out.push_back(text(" "));
    out.insert(out.end(),std::make_move_iterator(calculation.begin()),std::make_move_iterator(calculation.end()));
    return out;
}
Elements details(const app::CudaDidacticFrame& f,const std::string& error,int width,bool coverage,bool colors) {
    Elements out;
    heading(out,"CÓMO SE ASIGNA UNA CELDA",width,colors);out.push_back(text(" "));
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
    out.push_back(text(" "));
    append(out,"Las divisiones entre bloques no son fronteras muertas: los vecinos pueden cruzarlas.",width);
    append(out,"blockIdx="+pair(f.blockX,f.blockY)+" threadIdx="+pair(f.threadX,f.threadY)+" (base cero)",width);
    append(out,"columna = "+std::to_string(f.blockX)+" * "+std::to_string(f.geometry.block.x)+" + "+std::to_string(f.threadX)+" = "+std::to_string(f.column),width);
    append(out,"fila = "+std::to_string(f.blockY)+" * "+std::to_string(f.geometry.block.y)+" + "+std::to_string(f.threadY)+" = "+std::to_string(f.row),width);
    out.push_back(text(" "));
    heading(out,"CÁLCULO DE LA SIGUIENTE GENERACIÓN",width,colors);
    out.push_back(text(" "));
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
    out.push_back(text(" "));
    heading(out,"CONTROLES Y LEYENDA",width,colors);out.push_back(text(" "));
    append(out,"G avanza una generación CUDA real; la predicción CPU no escribe NEXT ni avanza el tablero.",width);
    append(out,"Tonos de fondo y marcas de la tabla: reparto por bloques, no orden de ejecución. La paleta se reutiliza; las coordenadas identifican cada bloque.",width);
    append(out,"Las celdas vivas conservan el verde. Los contornos de bloque usan el color de su marca; el bloque seleccionado tiene contorno cian.",width);
    append(out,"Borde doble amarillo: celda del hilo seleccionado; borde grueso azul: vecinos. Los hilos lanzados incluyen posiciones exteriores y no indican simultaneidad.",width);
    append(out,"Línea gruesa: límite real de bloque; fina: fin del tablero que corta bloque; discontinua: recorte del viewport, incluso si coincide con límite de bloque.",width);
    append(out,"A muestra cobertura en esquema compacto sin ampliar ni desplazar el tablero real. Si el hilo es exterior no se marca otra célula como sustituta.",width);
    if(f.selectionAdjusted)append(out,"Selección ajustada: la posición anterior no está en la nueva cobertura.",width);
    return out;
}
}
Element renderCudaDidactic(const Game& game,const app::CudaDidacticFrame& f,const std::string& gpu,
    const std::string& error,BoardViewport& viewport,bool detailOnly,std::size_t detailOffset,
    bool follow,int width,int height,const CudaDidacticPrimitives& primitives,bool coverage,bool playing,double speed) {
    Elements header;
    const auto accent=primitives.colors?Color::RGB(83,191,208):Color(Color::Default);
    const auto stateColor=primitives.colors?(playing?Color::RGB(98,232,137):Color::RGB(224,185,112)):Color(Color::Default);
    std::ostringstream rate;rate<<std::fixed<<std::setprecision(speed<.5?2:1)<<speed<<"x";
    header.push_back(hbox({text(std::string(width>=65?"Generación ":"G")+std::to_string(f.generation)),text("   "),
        text(playing?"Reproduciendo":"En pausa")|bold|color(stateColor),text("   "),text(rate.str())|bold|color(accent)}));
    const auto first=header.size();
    const auto block=std::to_string(f.geometry.block.x)+"x"+std::to_string(f.geometry.block.y);
    const auto grid=std::to_string(f.geometry.gridX)+"x"+std::to_string(f.geometry.gridY);
    append(header,width>=65?"Bloque "+block+"   Grid "+grid+"   Vista conceptual":
        "conceptual B"+block+" Grid "+grid+" A:"+(coverage?"+":"-"),width);
    for(auto i=first;i<header.size();++i) header[i]=header[i]|color(accent);
    if(height>=26) {header.push_back(legend(f,width,primitives.colors,coverage));header.push_back(text(" "));}
    const int remaining=std::max(1,height-static_cast<int>(header.size()));
    Element body;
    if(detailOnly) {
        auto all=selectionSummary(f,error,width,std::max(30,remaining),primitives.colors,coverage);
        all.push_back(text(" "));
        auto technical=details(f,error,width,coverage,primitives.colors);
        all.insert(all.end(),std::make_move_iterator(technical.begin()),std::make_move_iterator(technical.end()));
        Elements context;
        append(context,"GPU: "+gpu,width);
        append(context,"Exploración conceptual; no representa el orden de ejecución GPU.",width);
        append(context,"Espacio/P: reproducir o pausar generaciones CUDA reales. +/-: velocidad de 0.25x a 8.0x.",width);
        append(context,"G: paso manual y pausa. N: explorar otro hilo. R: reiniciar en pausa.",width);
        context.push_back(text(" "));
        all.insert(all.begin(),context.begin(),context.end());Elements visible;
        const int visibleRows=std::max(1,remaining-1);
        const auto start=std::min(detailOffset,all.size()>static_cast<std::size_t>(visibleRows)?all.size()-visibleRows:0);
        for(std::size_t i=start;i<all.size() && visible.size()<static_cast<std::size_t>(visibleRows);++i)visible.push_back(all[i]);
        body=vbox({vbox(std::move(visible))|flex,
            text("V: volver al tablero · PgUp/PgDn: desplazar")|dim});
    } else if(width>=110 && remaining>=22) {
        const int infoWidth=std::clamp(width/3,34,46);
        const int left=width-infoWidth-3;
        auto all=selectionSummary(f,error,infoWidth,remaining,primitives.colors,coverage);
        const bool scroll=all.size()>static_cast<std::size_t>(remaining);
        const auto visibleRows=static_cast<std::size_t>(std::max(1,remaining-(scroll?1:0)));
        const auto start=std::min(detailOffset,all.size()>visibleRows?all.size()-visibleRows:0);
        Elements info;
        for(auto i=start;i<all.size() && info.size()<visibleRows;++i) info.push_back(all[i]);
        if(scroll) info.push_back(text("PgUp/PgDn: desplazar cálculo")|dim);
        body=hbox({board(game,f,viewport,left,remaining,follow,primitives)|size(WIDTH,EQUAL,left),
                   separator(),text(" "),vbox(std::move(info))|size(WIDTH,EQUAL,infoWidth),text(" ")});
    } else {
        Elements summary;
        append(summary,std::string(f.selectionAdjusted?"Ajuste: ":"")+"b"+pair(f.blockX,f.blockY)+" t"+pair(f.threadX,f.threadY)+" → c"+std::to_string(f.column)+" f"+std::to_string(f.row),width);
        if(!error.empty())append(summary,"Error CUDA · V diagnóstico · U recupera",width);
        else if(f.cell.valid)append(summary,std::string(f.cell.evaluation.before?"Viva":"Muerta")+" · vecinos "+std::to_string(f.cell.evaluation.neighborCount)+" → "+(f.cell.evaluation.after?"viva":"muerta"),width);
        else append(summary,"Exterior: sin célula · G=CUDA real",width);
        const int boardHeight=remaining-static_cast<int>(summary.size());
        summary.push_back(board(game,f,viewport,width,boardHeight,follow,primitives)|flex);
        body=vbox(std::move(summary));
    }
    header.push_back(std::move(body)|flex);return vbox(std::move(header));
}
}

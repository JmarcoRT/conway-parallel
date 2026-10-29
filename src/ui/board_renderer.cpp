#include "ui/board_renderer.hpp"
#include <algorithm>

namespace conway::ui {
using namespace ftxui;
BoardGeometry computeBoardGeometry(std::size_t width,std::size_t height,std::size_t columns,std::size_t rows) {
    BoardGeometry g;
    if(width<3 || height<3 || !columns || !rows)return g;
    for(std::size_t cw=8;cw>=2;--cw) {
        const auto w=columns*(cw+1)+1,h=rows*2+1;
        if(w<=width && h<=height) {
            g.cell_chars=cw;g.visible_rows=rows;g.visible_cols=columns;
            g.board_chars=w;g.board_lines=h;g.full_board_shown=true;return g;
        }
    }
    g.visible_cols=std::min(columns,(width-1)/3);g.visible_rows=std::min(rows,(height-1)/2);
    g.board_chars=g.visible_cols*3+1;g.board_lines=g.visible_rows*2+1;return g;
}
BoardBoundary boardBoundary(std::size_t c,std::size_t extent,std::size_t first,std::size_t last,std::size_t block) {
    return {block && c%block==0,c==0 || c==extent,(c==first || c==last) && c!=0 && c!=extent};
}
Element markedBoardCell(bool alive,std::size_t width,BoardCellMark mark,bool colors) {
    width=std::clamp<std::size_t>(width,2,8);
    const std::string token=std::string(mark==BoardCellMark::Selected?"@":":")+(alive?"1":"0");
    const auto left=(width-2)/2;
    auto cell=text(std::string(left,' ')+token+std::string(width-2-left,' '));
    if(mark==BoardCellMark::Selected) {
        cell=cell|bold;
        if(colors)return cell|bgcolor(Color::RGB(255,209,102))|color(Color::RGB(25,32,38));
        return cell|inverted;
    }
    cell=cell|underlined;
    return colors?cell|bgcolor(Color::RGB(36,94,168))|color(Color::White):cell;
}
namespace {
std::string repeat(const char* value,std::size_t count) {
    std::string out;for(std::size_t i=0;i<count;++i)out+=value;return out;
}
}
Element renderBoardGrid(const BoardGeometry& g,std::size_t boardWidth,std::size_t boardHeight,
                       const BoardPrimitives& p,const BoardDecorations* d,std::string label) {
    if(!g.visible_rows || !g.visible_cols || !boardWidth || !boardHeight)return text("Tablero sin espacio")|dim;
    const auto cell=std::clamp<std::size_t>(g.cell_chars,2,8);
    auto selectedV=[&](std::size_t r,std::size_t c) {
        if(!d || !d->blockWidth || !d->blockHeight)return false;
        const auto x0=d->blockX*d->blockWidth,y0=d->blockY*d->blockHeight;
        if(x0>=boardWidth || y0>=boardHeight)return false;
        const auto x1=std::min(boardWidth,x0+d->blockWidth),y1=std::min(boardHeight,y0+d->blockHeight);
        return r>=y0 && r<y1 && (c==x0 || c==x1);
    };
    auto selectedH=[&](std::size_t r,std::size_t c) {
        if(!d || !d->blockWidth || !d->blockHeight)return false;
        const auto x0=d->blockX*d->blockWidth,y0=d->blockY*d->blockHeight;
        if(x0>=boardWidth || y0>=boardHeight)return false;
        const auto x1=std::min(boardWidth,x0+d->blockWidth),y1=std::min(boardHeight,y0+d->blockHeight);
        return c>=x0 && c<x1 && (r==y0 || r==y1);
    };
    auto line=[&](std::string value,bool selected,bool real) {
        // A base grid callback may already apply a foreground decorator. Build
        // selected segments directly so that it cannot override the contour.
        auto node=selected && d && d->colors
            ? text(std::move(value))|color(Color::RGB(83,191,208))
            : p.grid(std::move(value));
        if(real)node=node|bold;
        if(selected && d && !d->colors)node=node|inverted|bold;
        return node;
    };
    auto horizontal=[&](std::size_t r,bool top,bool bottom) {
        const char *left=top?"┌":bottom?"└":"├",*middle=top?"┬":bottom?"┴":"┼",*right=top?"┐":bottom?"┘":"┤";
        if(!d) {
            std::string value=left;
            for(std::size_t x=0;x<g.visible_cols;++x) {value+=repeat("─",cell);value+=x+1<g.visible_cols?middle:right;}
            return p.grid(std::move(value));
        }
        const auto edge=boardBoundary(r,boardHeight,g.row_offset,g.row_offset+g.visible_rows,d->blockHeight);
        Elements pieces;pieces.push_back(line(left,selectedH(r,g.col_offset),edge.realBlock&&!edge.viewportCut));
        for(std::size_t x=0;x<g.visible_cols;++x) {
            const auto c=g.col_offset+x;const bool selected=selectedH(r,c);
            const char* dash=edge.viewportCut?"┄":edge.realBlock?"━":"─";
            pieces.push_back(line(repeat(dash,cell),selected,edge.realBlock&&!edge.viewportCut));
            pieces.push_back(line(x+1<g.visible_cols?middle:right,selected,edge.realBlock&&!edge.viewportCut));
        }
        return hbox(std::move(pieces));
    };
    auto vertical=[&](std::size_t r,std::size_t c) {
        if(!d)return p.grid("│");
        const auto edge=boardBoundary(c,boardWidth,g.col_offset,g.col_offset+g.visible_cols,d->blockWidth);
        return line(edge.viewportCut?"┆":edge.realBlock?"┃":"│",selectedV(r,c),edge.realBlock&&!edge.viewportCut);
    };
    Elements rows;rows.reserve(g.visible_rows*2+2);rows.push_back(horizontal(g.row_offset,true,false));
    for(std::size_t y=0;y<g.visible_rows;++y) {
        const auto r=g.row_offset+y;Elements cells;cells.reserve(g.visible_cols*2+1);cells.push_back(vertical(r,g.col_offset));
        for(std::size_t x=0;x<g.visible_cols;++x) {
            const auto c=g.col_offset+x;
            const auto mark=d && d->mark?d->mark(r,c):BoardCellMark::None;
            if(mark!=BoardCellMark::None && r<boardHeight && c<boardWidth)cells.push_back(markedBoardCell(d->alive(r,c),cell,mark,d->colors));
            else cells.push_back(p.cell(r,c,cell));
            cells.push_back(vertical(r,c+1));
        }
        rows.push_back(hbox(std::move(cells)));rows.push_back(horizontal(r+1,false,y+1==g.visible_rows));
    }
    if(!label.empty())rows.push_back(text(std::move(label))|dim);
    return vbox({filler(),hbox({filler(),vbox(std::move(rows)),filler()}),filler()});
}
}

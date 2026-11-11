#include "ui/board_renderer.hpp"
#include <algorithm>

namespace conway::ui {
using namespace ftxui;
BoardGeometry computeBoardGeometry(std::size_t width,std::size_t height,std::size_t columns,std::size_t rows,
                                   std::size_t maxCellChars,bool compact,bool halfBlocks) {
    BoardGeometry g;
    if(!columns || !rows || width<(compact?1u:3u) || height<(compact?1u:3u))return g;
    if(compact) {
        // Adjacent cells, no interior grid. Prefer two columns per cell so the
        // board stays square-ish, but don't let it monopolize the available
        // width. If two columns still overflow, pack two logical rows into one
        // terminal line so the cells remain square instead of turning tall.
        const auto target=std::max<std::size_t>(20,width*3/4);
        for(std::size_t cw=std::clamp<std::size_t>(maxCellChars,2,8);cw>=2;--cw) {
            if(columns*cw>target)continue;
            for(bool border:{true,false}) {
                const auto w=columns*cw+(border?2:0),h=rows+(border?2:0);
                if(w<=width && h<=height) {
                    g.cell_chars=cw;g.compact=true;g.compact_border=border;
                    g.visible_rows=rows;g.visible_cols=columns;
                    g.board_chars=w;g.board_lines=h;g.full_board_shown=true;return g;
                }
            }
        }
        if(halfBlocks) {
            const auto halfRows=(rows+1)/2;
            for(bool border:{true,false}) {
                const auto w=columns+(border?2:0),h=halfRows+(border?2:0);
                if(w<=width && h<=height) {
                    g.cell_chars=1;g.compact=true;g.compact_border=border;g.half_block=true;
                    g.visible_rows=rows;g.visible_cols=columns;
                    g.board_chars=w;g.board_lines=h;g.full_board_shown=true;return g;
                }
            }
            // A compact viewport still shows more cells than the classic grid.
            g.cell_chars=1;g.compact=true;g.half_block=true;
            g.visible_cols=std::min(columns,width);
            g.visible_rows=std::min(rows,std::max<std::size_t>(1,height*2));
            g.board_chars=g.visible_cols;
            g.board_lines=(g.visible_rows+1)/2;
            return g;
        }
        g.cell_chars=1;g.compact=true;g.compact_border=false;
        g.visible_cols=std::min(columns,width);g.visible_rows=std::min(rows,height);
        g.board_chars=g.visible_cols;g.board_lines=g.visible_rows;return g;
    }
    for(std::size_t cw=std::clamp<std::size_t>(maxCellChars,2,8);cw>=2;--cw) {
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
    auto cell=text(std::string(width,' '));
    if(colors)cell=cell|bgcolor(alive?Color::RGB(98,232,137):Color::RGB(31,42,58));
    else if(alive)cell=cell|inverted;
    if(mark==BoardCellMark::Selected)return cell|bold|underlined;
    return mark==BoardCellMark::Neighbor?cell|underlined:cell;
}
namespace {
std::string repeat(const char* value,std::size_t count) {
    std::string out;for(std::size_t i=0;i<count;++i)out+=value;return out;
}
}
Element renderBoardGrid(const BoardGeometry& g,std::size_t boardWidth,std::size_t boardHeight,
                       const BoardPrimitives& p,const BoardDecorations* d,std::string label) {
    if(!g.visible_rows || !g.visible_cols || !boardWidth || !boardHeight)return text("Tablero sin espacio")|dim;
    if(g.compact) {
        const auto cell=std::clamp<std::size_t>(g.cell_chars,1,8);
        Elements rows;
        const auto step=g.half_block?2:1;
        rows.reserve((g.visible_rows+step-1)/step);
        for(std::size_t y=0;y<g.visible_rows;y+=step) {
            const auto r=g.row_offset+y;
            Elements cells;
            cells.reserve(g.visible_cols);
            for(std::size_t x=0;x<g.visible_cols;++x) {
                const auto c=g.col_offset+x;
                if(g.half_block && p.pair) {
                    const auto bottom=y+1<g.visible_rows?r+1:std::size_t(-1);
                    cells.push_back(p.pair(r,bottom,c,cell));
                    continue;
                }
                auto interior=p.cell(r,c,cell);
                const auto mark=d && d->mark?d->mark(r,c):BoardCellMark::None;
                if(mark==BoardCellMark::Selected)interior=interior|bold|inverted;
                else if(mark==BoardCellMark::Neighbor)interior=interior|underlined;
                cells.push_back(std::move(interior));
            }
            rows.push_back(hbox(std::move(cells)));
        }
        Element board=vbox(std::move(rows));
        if(g.compact_border)board=board|(g.compact_border_light?borderLight:border);
        if(!label.empty())board=vbox({std::move(board),text(std::move(label))|dim});
        return vbox({filler(),hbox({filler(),std::move(board),filler()}),filler()});
    }
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
    auto markAt=[&](std::size_t r,std::size_t c) {
        return d && d->mark && r<boardHeight && c<boardWidth ? d->mark(r,c):BoardCellMark::None;
    };
    auto strongest=[](BoardCellMark a,BoardCellMark b) {
        if(a==BoardCellMark::Selected || b==BoardCellMark::Selected)return BoardCellMark::Selected;
        if(a==BoardCellMark::Neighbor || b==BoardCellMark::Neighbor)return BoardCellMark::Neighbor;
        return BoardCellMark::None;
    };
    auto line=[&](std::string value,bool selected,bool real,BoardCellMark mark=BoardCellMark::None,
                 std::size_t row=0,std::size_t column=0) {
        if(mark!=BoardCellMark::None) {
            auto node=text(std::move(value))|bold;
            if(d && d->colors)node=node|color(mark==BoardCellMark::Selected?Color::RGB(255,209,102):Color::RGB(90,143,232));
            return node;
        }
        // A base grid callback may already apply a foreground decorator. Build
        // selected segments directly so that it cannot override the contour.
        auto node=selected && d && d->colors
            ? text(std::move(value))|color(Color::RGB(83,191,208))
            : real && d && d->colors && d->blockColor
            ? text(std::move(value))|color(d->blockColor(std::min(row,boardHeight-1),std::min(column,boardWidth-1)))
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
        Elements pieces;pieces.push_back(line(left,selectedH(r,g.col_offset),edge.realBlock&&!edge.viewportCut,BoardCellMark::None,r,g.col_offset));
        for(std::size_t x=0;x<g.visible_cols;++x) {
            const auto c=g.col_offset+x;const bool selected=selectedH(r,c);
            const auto mark=strongest(r?markAt(r-1,c):BoardCellMark::None,markAt(r,c));
            const char* dash=mark==BoardCellMark::Selected?"═":mark==BoardCellMark::Neighbor?"━":edge.viewportCut?"┄":edge.realBlock?"━":"─";
            pieces.push_back(line(repeat(dash,cell),selected,edge.realBlock&&!edge.viewportCut,mark,r,c));
            pieces.push_back(line(x+1<g.visible_cols?middle:right,selected,edge.realBlock&&!edge.viewportCut,BoardCellMark::None,r,c));
        }
        return hbox(std::move(pieces));
    };
    auto vertical=[&](std::size_t r,std::size_t c) {
        if(!d)return p.grid("│");
        const auto edge=boardBoundary(c,boardWidth,g.col_offset,g.col_offset+g.visible_cols,d->blockWidth);
        const auto mark=strongest(c?markAt(r,c-1):BoardCellMark::None,markAt(r,c));
        return line(mark==BoardCellMark::Selected?"║":mark==BoardCellMark::Neighbor?"┃":edge.viewportCut?"┆":edge.realBlock?"┃":"│",
            selectedV(r,c),edge.realBlock&&!edge.viewportCut,mark,r,c);
    };
    Elements rows;rows.reserve(g.visible_rows*2+2);rows.push_back(horizontal(g.row_offset,true,false));
    for(std::size_t y=0;y<g.visible_rows;++y) {
        const auto r=g.row_offset+y;Elements cells;cells.reserve(g.visible_cols*2+1);cells.push_back(vertical(r,g.col_offset));
        for(std::size_t x=0;x<g.visible_cols;++x) {
            const auto c=g.col_offset+x;
            const auto mark=d && d->mark?d->mark(r,c):BoardCellMark::None;
            auto interior=p.cell(r,c,cell);
            if(mark==BoardCellMark::Selected)interior=interior|bold;
            cells.push_back(std::move(interior));
            cells.push_back(vertical(r,c+1));
        }
        rows.push_back(hbox(std::move(cells)));rows.push_back(horizontal(r+1,false,y+1==g.visible_rows));
    }
    if(!label.empty())rows.push_back(text(std::move(label))|dim);
    return vbox({filler(),hbox({filler(),vbox(std::move(rows)),filler()}),filler()});
}
}

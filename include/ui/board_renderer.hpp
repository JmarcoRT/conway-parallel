#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <ftxui/dom/elements.hpp>

namespace conway::ui {
struct BoardGeometry {
    std::size_t cell_chars=2,visible_rows=0,visible_cols=0,row_offset=0,col_offset=0;
    std::size_t board_chars=0,board_lines=0;
    bool full_board_shown=false;
};
BoardGeometry computeBoardGeometry(std::size_t width,std::size_t height,std::size_t columns,std::size_t rows);
enum class BoardCellMark {None,Selected,Neighbor};
struct BoardBoundary {
    bool realBlock=false,boardEdge=false,viewportCut=false;
};
BoardBoundary boardBoundary(std::size_t coordinate,std::size_t extent,
                            std::size_t viewFirst,std::size_t viewLast,std::size_t blockSize);
struct BoardDecorations {
    std::size_t blockWidth=1,blockHeight=1,blockX=0,blockY=0;
    bool colors=false;
    std::function<BoardCellMark(std::size_t,std::size_t)> mark;
    std::function<bool(std::size_t,std::size_t)> alive;
};
struct BoardPrimitives {
    std::function<ftxui::Element(std::size_t,std::size_t,std::size_t)> cell;
    std::function<ftxui::Element(std::string)> grid;
};
ftxui::Element markedBoardCell(bool alive,std::size_t width,BoardCellMark,bool colors);
// Original grid/corners/centering. Decorations are optional and don't own state.
ftxui::Element renderBoardGrid(const BoardGeometry&,std::size_t boardWidth,std::size_t boardHeight,
    const BoardPrimitives&,const BoardDecorations* decorations=nullptr,std::string viewportLabel={});
}

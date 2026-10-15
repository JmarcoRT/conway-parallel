#pragma once
#include <algorithm>
#include <cstddef>

namespace conway::ui {
// Full approved composition requires its two columns and vertical chrome.
inline bool roomyTerminal(std::size_t columns, std::size_t rows) {
    return columns >= 120 && rows >= 40;
}
inline bool usableTerminal(std::size_t columns, std::size_t rows) {
    return columns >= 40 && rows >= 18;
}
// Grid cells occupy 3 columns and 2 lines. Reserve the existing frame/status
// budget and at least five logical rows (or the whole shorter pattern).
inline bool pairedSimulation(std::size_t columns, std::size_t layoutRows,
                             std::size_t boardWidth, std::size_t boardHeight) {
    const auto panel = std::max<std::size_t>(26, 3 * std::min<std::size_t>(boardWidth,17) + 3);
    return columns >= 2 * panel + 3 && layoutRows >= 17 + 2 * std::min<std::size_t>(boardHeight,5);
}
struct BoardViewport {
    std::size_t row{0}, column{0};
    void clamp(std::size_t height, std::size_t width, std::size_t rows, std::size_t columns) {
        row = std::min(row, height > rows ? height - rows : 0);
        column = std::min(column, width > columns ? width - columns : 0);
    }
    void move(int dy, int dx) {
        if (dy < 0) { if (row) --row; } else if (dy > 0) ++row;
        if (dx < 0) { if (column) --column; } else if (dx > 0) ++column;
    }
};
}

#include "ui/board_view.hpp"

#include <algorithm>
#include <string_view>

namespace conway::ui {

namespace {

void appendCell(std::string& line, bool alive, const BoardRenderOptions& options) {
    const std::size_t width = std::clamp(options.cellWidth, std::size_t{1}, std::size_t{6});
    if (options.colorEnabled) {
        line += alive ? "\033[42m" : "\033[100m";
        line.append(width, ' ');
        line += "\033[0m";
    } else {
        const std::string_view pixel = alive ? "█" : "░";
        for (std::size_t column = 0; column < width; ++column) {
            line += pixel;
        }
    }
}

std::vector<std::string> renderBoard(const conway::Game& game, const BoardRenderOptions& options) {
    const std::size_t columns = std::min(game.width(), options.maxColumns);
    const std::size_t rows = std::min(game.height(), options.maxRows);
    const std::size_t cellHeight = std::clamp(options.cellHeight, std::size_t{1}, std::size_t{3});
    std::vector<std::string> lines;
    lines.reserve(rows * cellHeight + (columns < game.width() ? 1U : 0U) +
                   (rows < game.height() ? 1U : 0U));

    for (std::size_t row = 0; row < rows; ++row) {
        std::string line;
        const std::size_t cellWidth = std::clamp(options.cellWidth, std::size_t{1}, std::size_t{6});
        line.reserve(columns * (cellWidth * (options.colorEnabled ? 12U : 3U) + 1U));
        for (std::size_t col = 0; col < columns; ++col) {
            appendCell(line, game.isAlive(row, col), options);
            if (col + 1 < columns) {
                line += ' ';
            }
        }
        for (std::size_t repeat = 0; repeat < cellHeight; ++repeat) {
            lines.push_back(line);
        }
    }

    if (columns < game.width() || rows < game.height()) {
        std::string viewport = "Viewport: ";
        if (columns < game.width()) {
            viewport += std::to_string(columns) + " de " +
                std::to_string(game.width()) + " columnas";
        }
        if (rows < game.height()) {
            if (columns < game.width()) {
                viewport += ", ";
            }
            viewport += std::to_string(rows) + " de " +
                std::to_string(game.height()) + " filas";
        }
        lines.push_back(std::move(viewport));
    }
    return lines;
}

} // namespace

std::vector<std::string> boardLines(const conway::Game& game) {
    std::vector<std::string> lines;
    lines.reserve(game.height());

    for (std::size_t row = 0; row < game.height(); ++row) {
        std::string line;
        line.reserve(game.width());
        for (std::size_t col = 0; col < game.width(); ++col) {
            line += game.isAlive(row, col) ? kAliveChar : kDeadChar;
        }
        lines.push_back(std::move(line));
    }

    return lines;
}

std::vector<std::string> renderBoardLines(
    const conway::Game& game,
    const BoardRenderOptions& options
) {
    return renderBoard(game, options);
}

std::vector<std::string> nextBoardLines(const conway::Game& game) {
    conway::Game preview = game;  
    preview.step();               
    return boardLines(preview);
}

std::vector<std::string> progressLines(const conway::Game& game, std::size_t totalGenerations) {
    return {
        "Generacion " + std::to_string(game.generation()) + " / " + std::to_string(totalGenerations),
        "Celulas vivas: " + std::to_string(game.aliveCount()),
    };
}

std::vector<std::string> legendLines() {
    return {
        std::string(1, kAliveChar) + " = celula viva",
        std::string(1, kDeadChar)  + " = celula muerta",
    };
}

void printSimulationView(std::ostream& out,
                         const conway::Game& game,
                         std::size_t totalGenerations) {
    for (const auto& line : progressLines(game, totalGenerations)) {
        out << line << '\n';
    }

    out << "\nCURRENT (generacion " << game.generation() << "):\n";
    for (const auto& line : boardLines(game)) {
        out << line << '\n';
    }

    out << "\nNEXT (generacion " << game.generation() + 1 << "):\n";
    for (const auto& line : nextBoardLines(game)) {
        out << line << '\n';
    }

    out << "\nLeyenda:\n";
    for (const auto& line : legendLines()) {
        out << "  " << line << '\n';
    }
}
} 
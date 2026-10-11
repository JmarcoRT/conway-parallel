#include "ui/board_view.hpp"

namespace conway::ui {

namespace {

void appendCell(std::string& line, bool alive, bool colorEnabled) {
    if (colorEnabled) {
        line += alive ? "\033[1;32m" : "\033[90m";
    }
    line += alive ? "[#]" : "[ ]";
    if (colorEnabled) {
        line += "\033[0m";
    }
}

std::vector<std::string> renderBoard(const conway::Game& game, const BoardRenderOptions& options) {
    const std::size_t columns = std::min(game.width(), options.maxColumns);
    const std::size_t rows = std::min(game.height(), options.maxRows);
    std::vector<std::string> lines;
    lines.reserve(rows + (columns < game.width() ? 1U : 0U) +
                   (rows < game.height() ? 1U : 0U));

    for (std::size_t row = 0; row < rows; ++row) {
        std::string line;
        line.reserve(columns * (options.colorEnabled ? 12U : 3U));
        for (std::size_t col = 0; col < columns; ++col) {
            appendCell(line, game.isAlive(row, col), options.colorEnabled);
        }
        lines.push_back(std::move(line));
    }

    if (columns < game.width()) {
        lines.emplace_back("… " + std::to_string(game.width() - columns) + " columnas omitidas");
    }
    if (rows < game.height()) {
        lines.emplace_back("… " + std::to_string(game.height() - rows) + " filas omitidas");
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
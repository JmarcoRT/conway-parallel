#pragma once

#include "core/game.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace conway::ui {

constexpr char kAliveChar = '#';
constexpr char kDeadChar  = '.';

struct BoardRenderOptions {
    std::size_t maxColumns{24};
    std::size_t maxRows{12};
    bool colorEnabled{false};
    std::size_t cellWidth{2};
    std::size_t cellHeight{1};
};

std::vector<std::string> boardLines(const conway::Game& game);

// Calculado por el core sobre una copia ;game no se modifica.
std::vector<std::string> nextBoardLines(const conway::Game& game);

std::vector<std::string> renderBoardLines(
    const conway::Game& game,
    const BoardRenderOptions& options
);

// "Generacion N / total" y "Celulas vivas: X".
std::vector<std::string> progressLines(const conway::Game& game, std::size_t totalGenerations);

std::vector<std::string> legendLines();

// Imprime la vista completa: progreso, CURRENT, NEXT y leyenda.
void printSimulationView(std::ostream& out, const conway::Game& game, std::size_t totalGenerations);
} 
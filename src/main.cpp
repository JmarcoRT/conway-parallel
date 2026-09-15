#include "core/game.hpp"

#include <iostream>

int main() {
    conway::Game game(10, 10);

    game.setCell(2, 3, true);
    game.setCell(4, 5, true);

    std::cout << "Tablero: "
              << game.width() << "x" << game.height() << '\n';

    std::cout << "Celulas vivas: "
              << game.aliveCount() << '\n';

    std::cout << "Celda (2,3): "
              << (game.isAlive(2, 3) ? "viva" : "muerta") << '\n';

    std::cout << "Edad (2,3): "
              << game.age(2, 3) << '\n';

    return 0;
}
#include "core/game.hpp"
#include "input/input.hpp"

#include <iostream>

void printGame(const conway::Game& game) {
    for (std::size_t row = 0; row < game.height(); ++row) {
        for (std::size_t col = 0; col < game.width(); ++col) {
            std::cout << (game.isAlive(row, col) ? '1' : '0');
        }

        std::cout << '\n';
    }
}

int main() {
    try {
        InputData input = loadInput("patterns/test.txt");
        conway::Game game(input);

        std::cout << "Generacion "
                  << game.generation()
                  << ":\n";

        printGame(game);

        game.step();

        std::cout << "\nGeneracion "
                  << game.generation()
                  << ":\n";

        printGame(game);

        game.step();

        std::cout << "\nGeneracion "
                << game.generation()
                << ":\n";

        printGame(game);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: "
                  << e.what()
                  << '\n';

        return 1;
    }

    return 0;
}
// Demo S6: visualizacion de current / next / progreso con un patron conocido.
// Uso: conway_view_demo [patron.txt] [generaciones] por defecto: patterns/blinker.txt, 4 generaciones.

#include "core/game.hpp"
#include "input/input.hpp"
#include "ui/board_view.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : "patterns/blinker.txt";
    const std::size_t total =
        (argc > 2) ? static_cast<std::size_t>(std::stoul(argv[2])) : 4;

    try {
        conway::Game game(loadInput(path));

        std::cout << "=== Patron: " << path << " ("
                  << game.width() << " x " << game.height() << ") ===\n";

        for (std::size_t g = 0; g <= total; ++g) {
            std::cout << "\n----------------------------------------\n";
            conway::ui::printSimulationView(std::cout, game, total);
            game.step();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

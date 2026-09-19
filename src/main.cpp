#include "input/input.hpp"

#include <iostream>

int main() {
    try {
        InputData input = loadInput("patterns/test.txt");

        std::cout << "Tablero: "
                  << input.height
                  << "x"
                  << input.width
                  << '\n';

        std::cout << "Celdas cargadas: "
                  << input.cells.size()
                  << '\n';
    }
    catch (const std::exception& e) {
        std::cerr << "Error: "
                  << e.what()
                  << '\n';

        return 1;
    }

    return 0;
}
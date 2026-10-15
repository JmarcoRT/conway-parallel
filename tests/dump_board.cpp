// Utilidad de apoyo para el criterio de equivalencia (docs/TESTING.md).
//
// No reemplaza a `main` ni a ningun motor: unicamente carga un tablero con
// el `input` existente, lo avanza N generaciones con el `core` secuencial
// (sin duplicar su algoritmo) y escribe el resultado en un formato de texto
// simple y estable, pensado para poder compararse por lineas (diff) contra
// el volcado que produzcan, mas adelante, las variantes OpenMP, Pthreads y
// MPI una vez que existan.
//
// Formato de salida (contrato estable):
//   linea 1: "<ancho> <alto> <generacion> <vivas>"
//   lineas siguientes: <alto> filas de <ancho> caracteres '0'/'1'
//
// Uso: dump_board <archivo_de_entrada> <generaciones>

#include "core/game.hpp"
#include "input/input.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "Uso: dump_board <archivo_de_entrada> <generaciones>\n";
            return 2;
        }

        const std::string path = argv[1];
        const int generations = std::stoi(argv[2]);
        if (generations < 0) {
            std::cerr << "El numero de generaciones no puede ser negativo\n";
            return 2;
        }

        InputData input = loadInput(path);
        conway::Game game(input);

        for (int i = 0; i < generations; ++i) {
            game.step();
        }

        std::cout << game.width() << ' ' << game.height() << ' '
                   << game.generation() << ' ' << game.aliveCount() << '\n';

        for (std::size_t row = 0; row < game.height(); ++row) {
            for (std::size_t col = 0; col < game.width(); ++col) {
                std::cout << (game.isAlive(row, col) ? '1' : '0');
            }
            std::cout << '\n';
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

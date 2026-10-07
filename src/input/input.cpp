#include "input/input.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

InputData loadInput(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error(
            "No se pudo abrir el archivo: " + path
        );
    }

    std::size_t height;
    std::size_t width;

    if (!(file >> height >> width)) {
        throw std::runtime_error(
            "No se pudieron leer las dimensiones del tablero."
        );
    }

    if (height == 0 || width == 0) {
        throw std::runtime_error(
            "Las dimensiones deben ser mayores que cero."
        );
    }

    std::vector<std::uint8_t> cells;
    cells.reserve(width * height);

    std::string row;

    for (std::size_t r = 0; r < height; ++r) {
        if (!(file >> row)) {
            throw std::runtime_error(
                "Faltan filas en el tablero."
            );
        }

        if (row.size() != width) {
            throw std::runtime_error(
                "La fila " + std::to_string(r + 1) +
                " no tiene el ancho esperado."
            );
        }

        for (char cell : row) {
            if (cell != '0' && cell != '1') {
                throw std::runtime_error(
                    "El tablero solo puede contener 0 y 1."
                );
            }

            cells.push_back(
                static_cast<std::uint8_t>(cell - '0')
            );
        }
    }

    std::string extra;

    if (file >> extra) {
        throw std::runtime_error(
            "El archivo contiene datos adicionales despues del tablero."
        );
    }

    return InputData{
        width,
        height,
        std::move(cells)
    };
}
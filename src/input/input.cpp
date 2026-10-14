#include "input/input.hpp"
#include "input/validated_dimensions.hpp"
#include <charconv>
#include <string_view>

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

    std::string heightToken, widthToken;
    if (!(file >> heightToken >> widthToken))
        throw std::runtime_error("No se pudieron leer las dimensiones del tablero.");
    auto dimension = [](std::string_view token) {
        if (!token.empty() && token.front() == '+') token.remove_prefix(1);
        std::size_t value{};
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
            throw std::runtime_error("No se pudieron leer las dimensiones del tablero: valor no representable.");
        return value;
    };
    const auto height = dimension(heightToken), width = dimension(widthToken);
    conway::input::detail::BoardDimensions shape;
    try { shape = conway::input::detail::validateDimensions(width, height); }
    catch (const std::logic_error& error) { throw std::runtime_error(error.what()); }
    std::vector<std::uint8_t> cells;
    cells.reserve(shape.cells);

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

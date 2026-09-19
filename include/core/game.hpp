#pragma once

#include "input/input.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway {

class Game {
public:
    explicit Game(const InputData& input);

    std::size_t width() const noexcept;
    std::size_t height() const noexcept;
    std::size_t generation() const noexcept;
    std::size_t aliveCount() const noexcept;

    bool isAlive(std::size_t row, std::size_t col) const;

    void setCell(std::size_t row, std::size_t col, bool alive);
    void clear();

    void step();

private:
    std::size_t width_;
    std::size_t height_;
    std::size_t stride_;

    std::size_t generation_{0};
    std::size_t aliveCount_{0};

    std::vector<std::uint8_t> current_;
    std::vector<std::uint8_t> next_;

    std::size_t index(
        std::size_t row,
        std::size_t col
    ) const;
};

}
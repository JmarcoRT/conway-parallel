#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway {

class Game {
public:
    Game(std::size_t width, std::size_t height);

    std::size_t width() const noexcept;
    std::size_t height() const noexcept;
    std::size_t generation() const noexcept;
    std::size_t aliveCount() const noexcept;

    bool isAlive(std::size_t row, std::size_t col) const;
    std::uint16_t age(std::size_t row, std::size_t col) const;

    void setCell(std::size_t row, std::size_t col, bool alive);
    void clear();

private:
    std::size_t width_;
    std::size_t height_;
    std::size_t generation_{0};
    std::size_t aliveCount_{0};

    std::vector<std::uint8_t> cells_;
    std::vector<std::uint16_t> ages_;

    std::size_t index(std::size_t row, std::size_t col) const;
};

}
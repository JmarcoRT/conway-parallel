#include "core/game.hpp"

#include <algorithm>
#include <stdexcept>

namespace conway {

Game::Game(std::size_t width, std::size_t height)
    : width_(width),
      height_(height),
      cells_(width * height, 0),
      ages_(width * height, 0) {

    if (width == 0 || height == 0) {
        throw std::invalid_argument("El tablero debe tener dimensiones mayores que cero");
    }
}

std::size_t Game::width() const noexcept {
    return width_;
}

std::size_t Game::height() const noexcept {
    return height_;
}

std::size_t Game::generation() const noexcept {
    return generation_;
}

std::size_t Game::aliveCount() const noexcept {
    return aliveCount_;
}

bool Game::isAlive(std::size_t row, std::size_t col) const {
    return cells_.at(index(row, col)) != 0;
}

std::uint16_t Game::age(std::size_t row, std::size_t col) const {
    return ages_.at(index(row, col));
}

void Game::setCell(std::size_t row, std::size_t col, bool alive) {
    const std::size_t i = index(row, col);
    const bool wasAlive = cells_.at(i) != 0;

    if (wasAlive == alive) {
        return;
    }

    cells_[i] = alive ? 1 : 0;
    ages_[i] = alive ? 1 : 0;

    if (alive) {
        ++aliveCount_;
    } else {
        --aliveCount_;
    }
}

void Game::clear() {
    std::fill(cells_.begin(), cells_.end(), 0);
    std::fill(ages_.begin(), ages_.end(), 0);

    generation_ = 0;
    aliveCount_ = 0;
}

std::size_t Game::index(std::size_t row, std::size_t col) const {
    if (row >= height_ || col >= width_) {
        throw std::out_of_range("Celda fuera de los limites del tablero");
    }

    return row * width_ + col;
}

}
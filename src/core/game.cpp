#include "core/game.hpp"

#include <algorithm>
#include <stdexcept>

namespace conway {

Game::Game(const InputData& input)
    : width_(input.width),
      height_(input.height),
      stride_(input.width + 2),
      current_((input.width + 2) * (input.height + 2), 0),
      next_((input.width + 2) * (input.height + 2), 0) {

    if (width_ == 0 || height_ == 0) {
        throw std::invalid_argument(
            "El tablero debe tener dimensiones mayores que cero"
        );
    }

    for (std::size_t row = 0; row < height_; ++row) {
        for (std::size_t col = 0; col < width_; ++col) {

            const std::size_t inputIndex =
                row * width_ + col;

            const std::size_t gameIndex =
                (row + 1) * stride_ + (col + 1);

            current_[gameIndex] =
                input.cells[inputIndex];

            if (current_[gameIndex] != 0) {
                ++aliveCount_;
            }
        }
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
    return current_.at(index(row, col)) != 0;
}

void Game::setCell(std::size_t row, std::size_t col, bool alive) {
    const std::size_t i = index(row, col);
    const bool wasAlive = current_.at(i) != 0;

    if (wasAlive == alive) {
        return;
    }

    current_[i] = alive ? 1 : 0;

    if (alive) {
        ++aliveCount_;
    } else {
        --aliveCount_;
    }
}

void Game::clear() {
    std::fill(current_.begin(), current_.end(), 0);
    std::fill(next_.begin(), next_.end(), 0);

    generation_ = 0;
    aliveCount_ = 0;
}

std::size_t Game::index(
    std::size_t row,
    std::size_t col
) const {

    if (row >= height_ || col >= width_) {
        throw std::out_of_range(
            "Celda fuera de los limites del tablero"
        );
    }

    return (row + 1) * stride_ + (col + 1);
}

void Game::step() {
    std::size_t nextAliveCount = 0;

    for (std::size_t row = 1; row <= height_; ++row) {
        for (std::size_t col = 1; col <= width_; ++col) {

            const std::size_t i =
                row * stride_ + col;

            const int neighbors =
                  current_[i - stride_ - 1]
                + current_[i - stride_]
                + current_[i - stride_ + 1]
                + current_[i - 1]
                + current_[i + 1]
                + current_[i + stride_ - 1]
                + current_[i + stride_]
                + current_[i + stride_ + 1];

            next_[i] =
                (neighbors == 3) ||
                (current_[i] != 0 && neighbors == 2);

            if (next_[i] != 0) {
                ++nextAliveCount;
            }
        }
    }

    current_.swap(next_);

    aliveCount_ = nextAliveCount;
    ++generation_;
}

}
#include "core/bs23.hpp"

namespace conway::core {

std::uint8_t next_cell_state(
    const std::uint8_t* current,
    std::size_t stride,
    std::size_t row,
    std::size_t col) {

    const std::size_t i = row * stride + col;

    const int neighbors =
          current[i - stride - 1]
        + current[i - stride]
        + current[i - stride + 1]
        + current[i - 1]
        + current[i + 1]
        + current[i + stride - 1]
        + current[i + stride]
        + current[i + stride + 1];

    const std::uint8_t cell = current[i];
    return ((neighbors == 3) || (cell != 0 && neighbors == 2)) ? std::uint8_t{1}
                                                                : std::uint8_t{0};
}

} // namespace conway::core

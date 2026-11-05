#include "core/bs23.hpp"
#include "core/detail/bs23_rule.hpp"

namespace conway::core {
std::uint8_t next_cell_state(const std::uint8_t* current, std::size_t stride,
                            std::size_t row, std::size_t col) {
    return detail::evaluate_cell(current, stride, row, col).after;
}
std::uint8_t next_cell_state(const std::uint8_t* current, std::size_t stride,
                            std::size_t row, std::size_t col, CellEvaluation& capture) {
    const auto result = detail::evaluate_cell(current, stride, row, col);
    capture.before = result.before;
    for (unsigned k = 0; k < 8; ++k) capture.neighbors[k] = result.neighbors[k];
    capture.neighborCount = result.count;
    capture.after = result.after;
    return result.after;
}
} // namespace conway::core

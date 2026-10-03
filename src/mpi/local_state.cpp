#include "mpi/local_state.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace conway::mpi {

core::GameState make_local_state_from_rows(std::size_t width,
                                           const RowPartition& part,
                                           const std::vector<std::uint8_t>& rows) {
    if (width == 0 || rows.size() != width * part.rowCount) {
        throw std::invalid_argument(
            "make_local_state_from_rows: tamano de filas inconsistente con la particion");
    }

    core::GameState s{};
    s.width      = width;
    s.height     = part.rowCount;
    s.stride     = width + 2;
    s.generation = 0;
    s.aliveCount = 0;
    s.current.assign(s.stride * (s.height + 2), 0);
    s.next.assign(s.stride * (s.height + 2), 0);

    for (std::size_t i = 0; i < s.height; ++i) {
        const std::uint8_t* src = rows.data() + i * width;
        std::uint8_t* dst = s.current.data() + (i + 1) * s.stride + 1;
        std::copy(src, src + width, dst);
        for (std::size_t c = 0; c < width; ++c) {
            s.aliveCount += (src[c] != 0) ? 1 : 0;
        }
    }
    return s;
}

core::GameState make_local_state(const InputData& input,
                                 const RowPartition& part) {
    if (part.globalHeight != input.height ||
        part.endRow() > input.height ||
        input.cells.size() != input.width * input.height) {
        throw std::invalid_argument(
            "make_local_state: la particion no corresponde a la entrada");
    }
    const auto first = input.cells.begin() +
        static_cast<std::ptrdiff_t>(part.firstRow * input.width);
    const std::vector<std::uint8_t> rows(
        first, first + static_cast<std::ptrdiff_t>(part.rowCount * input.width));
    return make_local_state_from_rows(input.width, part, rows);
}

std::vector<std::uint8_t> pack_local_rows(const core::GameState& s) {
    std::vector<std::uint8_t> out(s.width * s.height);
    for (std::size_t i = 0; i < s.height; ++i) {
        const std::uint8_t* src = s.current.data() + (i + 1) * s.stride + 1;
        std::copy(src, src + s.width, out.data() + i * s.width);
    }
    return out;
}

} // namespace conway::mpi

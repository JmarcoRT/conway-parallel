#pragma once

#include <cstddef>
#include <cstdint>

// No runtime headers or device storage enter the common core. Ordinary C++
// compilers see an ordinary inline function; nvcc also emits the device version.
#if defined(__CUDACC__)
#define CONWAY_BS23_HD __host__ __device__
#else
#define CONWAY_BS23_HD
#endif

namespace conway::core::detail {
struct CellResult {
    std::uint8_t before;
    std::uint8_t neighbors[8];
    std::uint8_t count;
    std::uint8_t after;
};

CONWAY_BS23_HD inline CellResult evaluate_cell(const std::uint8_t* current,
                                              std::size_t stride,
                                              std::size_t row,
                                              std::size_t col) {
    const auto i = row * stride + col;
    CellResult result{current[i], {current[i-stride-1], current[i-stride], current[i-stride+1],
                                  current[i-1], current[i+1],
                                  current[i+stride-1], current[i+stride], current[i+stride+1]}, 0, 0};
    // Preserve the original helper's integer arithmetic even for raw byte
    // buffers supplied outside Game's binary-input validation.
    int neighbors = 0;
    for (unsigned k = 0; k < 8; ++k) neighbors += result.neighbors[k];
    result.count = static_cast<std::uint8_t>(neighbors);
    result.after = (neighbors == 3 || (result.before != 0 && neighbors == 2)) ? 1 : 0;
    return result;
}
} // namespace conway::core::detail

#undef CONWAY_BS23_HD

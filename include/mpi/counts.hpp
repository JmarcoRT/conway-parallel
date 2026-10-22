#pragma once

#include "input/validated_dimensions.hpp"
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace conway::mpi {

// Validate BEFORE multiplying, allocating or narrowing Scatterv/Gatherv
// counts/displacements. A full-board bound also bounds every contiguous slice.
inline std::size_t checked_cell_count(std::size_t width, std::size_t height) {
    const auto limit = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (!width || !height || width > limit / height)
        throw std::invalid_argument("dimensiones fuera del rango de conteos int de MPI");
    return input::detail::validateDimensions(width, height).cells;
}

} // namespace conway::mpi

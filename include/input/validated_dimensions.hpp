#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace conway::input::detail {
struct BoardDimensions { std::size_t cells, stride, padded; };
// Shared representability checks, before allocations. No host-memory policy:
// vector capacity and engine index bounds are contracts; bad_alloc may remain.
inline BoardDimensions validateDimensions(std::size_t width, std::size_t height) {
    if (!width || !height)
        throw std::invalid_argument("Las dimensiones deben ser mayores que cero.");
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (height >= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())
        || width > maximum - 2 || height > maximum - 2 || width > maximum / height)
        throw std::length_error("Las dimensiones exceden el rango representable.");
    const auto count = width * height;
    const auto stride = width + 2;
    if (stride > maximum / (height + 2))
        throw std::length_error("Las dimensiones del halo desbordan el rango representable.");
    const auto padded = stride * (height + 2);
    const auto capacity = std::vector<std::uint8_t>{}.max_size();
    if (count > capacity || padded > capacity || padded > maximum / 2)
        throw std::length_error("Las dimensiones exceden la capacidad de los buffers.");
    return {count, stride, padded};
}
}

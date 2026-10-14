#pragma once

#include "input/input.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace conway::input {

inline constexpr std::string_view kGeneratorVersion = "mt19937-row-major-rejection-v1";

struct GenerationConfig {
    std::size_t width;
    std::size_t height;
    int density; // Probability in units of 1/10000, inclusive range [0, 10000].
    std::uint32_t seed;
};

struct GenerationLimits {
    // Payload budget: InputData plus the two padded byte buffers of one Game.
    // Configurable policy, not a guarantee about available RAM or process RSS.
    std::size_t max_memory_bytes = 256u * 1024u * 1024u;
};

struct GenerationMetadata {
    std::size_t width;
    std::size_t height;
    std::uint32_t seed;
    int requested_density;
    std::size_t alive_count;
    double observed_density; // Fraction [0, 1], not a percentage.
    std::string_view version;
};

struct GeneratedInput {
    InputData input;
    GenerationMetadata metadata;
};

// Invalid parameters: invalid_argument. Unsafe sizes/budget: length_error.
// Allocation failure after validation propagates bad_alloc. No engine/UI dependency.
GeneratedInput generateInput(const GenerationConfig& config,
                             const GenerationLimits& limits = {});

} // namespace conway::input

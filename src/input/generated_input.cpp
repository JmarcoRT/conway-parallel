#include "input/generated_input.hpp"

#include <limits>
#include <random>
#include <stdexcept>

namespace conway::input {
namespace {
std::size_t checkedAdd(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        throw std::length_error("Generated input size addition overflows");
    return a + b;
}

std::size_t checkedMultiply(std::size_t a, std::size_t b) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::length_error("Generated input size multiplication overflows");
    return a * b;
}
} // namespace

GeneratedInput generateInput(const GenerationConfig& config, const GenerationLimits& limits) {
    if (config.width == 0 || config.height == 0)
        throw std::invalid_argument("Generated input dimensions must be positive");
    if (config.density < 0 || config.density > 10000)
        throw std::invalid_argument("Generated input density must be in [0, 10000]");

    // OpenMP's signed row loop increments once beyond height.
    if (config.height >= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        throw std::length_error("Generated input height exceeds engine index range");
    const auto count = checkedMultiply(config.width, config.height);
    const auto padded = checkedMultiply(checkedAdd(config.width, 2),
                                        checkedAdd(config.height, 2));
    const auto capacity = std::vector<std::uint8_t>{}.max_size();
    if (count > capacity || padded > capacity)
        throw std::length_error("Generated input exceeds vector capacity");
    const auto payload = checkedAdd(count, checkedMultiply(2, padded));
    if (payload > limits.max_memory_bytes)
        throw std::length_error("Generated input and Game buffers exceed memory budget");

    GeneratedInput result{
        InputData{config.width, config.height, std::vector<std::uint8_t>(count)},
        {config.width, config.height, config.seed, config.density, 0, 0.0, kGeneratorVersion}};
    std::mt19937 random(config.seed);
    // Largest multiple of 10000 below 2^32. Never narrow 2^32 to uint32_t.
    constexpr std::uint64_t acceptance_limit = ((std::uint64_t{1} << 32) / 10000) * 10000;
    for (auto& cell : result.input.cells) { // Flat storage is row-major.
        std::uint32_t sample;
        do {
            sample = static_cast<std::uint32_t>(random());
        } while (sample >= acceptance_limit);
        cell = static_cast<std::uint8_t>(sample % 10000 < static_cast<unsigned>(config.density));
        result.metadata.alive_count += cell;
    }
    result.metadata.observed_density = static_cast<double>(result.metadata.alive_count)
                                       / static_cast<double>(count);
    return result;
}
} // namespace conway::input

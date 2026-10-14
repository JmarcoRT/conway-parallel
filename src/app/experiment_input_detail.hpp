#pragma once

// Shared validation/identity for experiment inputs; no board allocation.
#include "app/experiment_runner.hpp"
#include <limits>
#include <stdexcept>

namespace conway::app::detail {
inline std::size_t checkedAdd(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        throw std::length_error("Experiment size addition overflows");
    return a + b;
}
inline std::size_t checkedMultiply(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::length_error("Experiment size multiplication overflows");
    return a * b;
}

inline ExperimentInputIdentity validateInput(const InputData& input) {
    if (!input.width || !input.height)
        throw std::invalid_argument("Experiment dimensions must be positive");
    if (input.height >= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        throw std::length_error("Experiment height exceeds engine index range");
    const auto count = checkedMultiply(input.width, input.height);
    const auto padded = checkedMultiply(checkedAdd(input.width, 2), checkedAdd(input.height, 2));
    if (padded > std::vector<std::uint8_t>{}.max_size())
        throw std::length_error("Experiment halo exceeds vector capacity");
    // Peak board payload: borrowed input + reference Game + current Game.
    (void)checkedAdd(count, checkedMultiply(4, padded));
    if (input.cells.size() != count)
        throw std::invalid_argument("Experiment cell count does not match dimensions");

    std::uint64_t fingerprint = 14695981039346656037ULL;
    const auto append = [&](std::uint8_t byte) {
        fingerprint ^= byte;
        fingerprint *= 1099511628211ULL; // Defined uint64_t wraparound.
    };
    static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
    for (auto dimension : {input.width, input.height}) {
        for (int shift = 0; shift < 64; shift += 8)
            append(static_cast<std::uint8_t>(static_cast<std::uint64_t>(dimension) >> shift));
    }
    std::size_t alive = 0;
    for (auto cell : input.cells) {
        if (cell > 1) throw std::invalid_argument("Experiment requires binary cells");
        alive += cell;
        append(cell);
    }
    return {input.width, input.height, alive, fingerprint, std::nullopt};
}

inline void attachGeneratedMetadata(ExperimentInputIdentity& identity, const InputData& input,
                                    const input::GenerationMetadata* generated) {
    if (generated) {
        const double observed = static_cast<double>(identity.aliveCount) / static_cast<double>(input.cells.size());
        if (generated->width != input.width || generated->height != input.height
            || generated->alive_count != identity.aliveCount
            || generated->requested_density < 0 || generated->requested_density > 10000
            || generated->observed_density != observed || generated->version.empty())
            throw std::invalid_argument("Generated input metadata does not match input");
        identity.generated = GeneratedInputIdentity{generated->seed, generated->requested_density,
                                                    observed, std::string(generated->version)};
    }
}
} // namespace conway::app::detail

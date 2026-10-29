#include "core/cuda_launch.hpp"
#include "input/validated_dimensions.hpp"

namespace conway::core {
CudaLaunchGeometry planCudaLaunch(std::size_t width, std::size_t height,
                                        CudaBlockSize block) {
    validateCudaBlock(block);
    const auto shape = input::detail::validateDimensions(width, height);
    const auto gx = (width - 1) / block.x + 1;
    const auto gy = (height - 1) / block.y + 1;
    const std::size_t threads = block.x * block.y;
    const auto multiply = [](std::size_t a, std::size_t b) {
        if (a > std::numeric_limits<std::size_t>::max() / b)
            throw std::overflow_error("CUDA launch position count overflow");
        return a * b;
    };
    const auto positions = multiply(multiply(gx, gy), threads);
    return {block, threads, gx, gy, positions, positions - shape.cells};
}
} // namespace conway::core

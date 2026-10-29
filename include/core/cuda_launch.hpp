#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace conway::core {
// x maps to columns, y to rows. z is always 1.
struct CudaBlockSize {
    unsigned x, y;
    bool operator==(const CudaBlockSize&) const = default;
};
inline constexpr CudaBlockSize kCudaBlock8x8{8, 8};
inline constexpr CudaBlockSize kCudaBlock16x16{16, 16};
inline constexpr CudaBlockSize kCudaBlock32x8{32, 8};
inline constexpr CudaBlockSize kDefaultCudaBlock = kCudaBlock16x16;

inline void validateCudaBlock(CudaBlockSize block) {
    if (block != kCudaBlock8x8 && block != kCudaBlock16x16 && block != kCudaBlock32x8)
        throw std::invalid_argument("CUDA block must be 8x8, 16x16 or 32x8");
}

struct CudaLaunchGeometry {
    CudaBlockSize block;
    std::size_t threadsPerBlock;
    std::size_t gridX, gridY; // z = 1
    std::size_t launchedPositions;
    std::size_t outsidePositions; // Logical positions outside width x height, not halos.
    bool operator==(const CudaLaunchGeometry&) const = default;
};

// Pure planned geometry: no runtime/device validation. Launch positions are
// NOT simultaneous execution, occupancy, physical threads or measured work.
CudaLaunchGeometry planCudaLaunch(std::size_t width, std::size_t height,
                                  CudaBlockSize block = kDefaultCudaBlock);
} // namespace conway::core

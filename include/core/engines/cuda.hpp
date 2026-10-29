#pragma once

#include "core/game_state.hpp"
#include "core/cuda_launch.hpp"
#include "input/input.hpp"
#include <memory>
#include <stdexcept>
#include <string>

namespace conway::core {

enum class CudaAvailability { Available, NoDevice, DriverUnavailable, RuntimeError };
struct CudaDeviceStatus {
    CudaAvailability availability;
    int deviceCount;
    int errorCode;
    std::string diagnostic;
};

// Toolkit availability is a build-time condition (CONWAY_ENABLE_CUDA).
// This query concerns the runtime only. Driver/runtime errors are not NoDevice.
CudaDeviceStatus cuda_device_status();

class CudaError : public std::runtime_error {
public:
    CudaError(int code, const std::string& message);
    int code() const noexcept { return code_; }
private:
    int code_;
};

// Resident engine used independently or by the optional Game CUDA dispatch.
// Move-only GPU ownership. Snapshots own their CPU vectors and never alias GPU
// resources or another snapshot. Calls on one instance must be serialized.
class CudaBackend {
public:
    explicit CudaBackend(const InputData& input, std::size_t generation = 0, int device = 0,
                         CudaBlockSize block = kDefaultCudaBlock);
    ~CudaBackend();
    CudaBackend(CudaBackend&&) noexcept;
    CudaBackend& operator=(CudaBackend&&) noexcept;
    CudaBackend(const CudaBackend&) = delete;
    CudaBackend& operator=(const CudaBackend&) = delete;

    // Synchronous publication: check launch/execution errors before swapping
    // buffers or advancing generation (size_t wrap, as in Game).
    // No allocation or board transfer per step.
    void step();
    GameState snapshot() const;

    // Validate against this device/kernel without allocating GPU board resources.
    static CudaLaunchGeometry launchConfiguration(std::size_t width, std::size_t height,
                                                 CudaBlockSize block = kDefaultCudaBlock,
                                                 int device = 0);
    CudaLaunchGeometry launchConfiguration() const;
    static std::string deviceName(int device = 0);
    // Transactional: changes only launch geometry, preserves both GPU buffers.
    void setBlockSize(CudaBlockSize block);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace conway::core

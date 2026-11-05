#pragma once
#include "core/cuda_launch.hpp"
#include "core/game_state.hpp"
#include "input/input.hpp"
#include <stdexcept>
#include <string>

namespace conway::app {
struct CudaMeasurement {
    double deviceMs{}, prepareMs{}, snapshotMs{}, totalMs{};
    std::size_t gridX{}, gridY{}, threadsPerBlock{}, outsidePositions{}, totalMemoryBytes{};
    int device{}, major{}, minor{}, runtimeVersion{}, driverApiVersion{};
    std::string deviceName, nvccVersion;
    bool cleanupConfirmed{};
};
struct CudaBenchmarkRun {core::GameState finalState;double elapsedMs{};CudaMeasurement observed;};
class CudaBenchmarkError : public std::runtime_error {
public:
    CudaBenchmarkError(const std::string& message,bool clean):std::runtime_error(message),cleanupConfirmed(clean) {}
    bool cleanupConfirmed;
};
// Preflight performs no board allocation and fails explicitly in CPU-only builds.
void validateCudaBenchmark(std::size_t width,std::size_t height,core::CudaBlockSize block);
CudaBenchmarkRun runCudaBenchmark(const InputData&,int generations,core::CudaBlockSize);
}

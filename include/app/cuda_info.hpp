#pragma once
#include "core/cuda_launch.hpp"
#include <optional>
#include <string>

namespace conway::app {
struct CudaUiInfo {
    bool available{false};
    std::string diagnostic{"CUDA: disponibilidad sin consultar"};
    std::string gpu;
    std::optional<core::CudaLaunchGeometry> geometry;
};
// Explicit action-time check: never call from rendering. Probes initialization
// on a 1x1 board, then validates geometry for the selected board; no evolution.
CudaUiInfo inspectCuda(std::size_t width=1,std::size_t height=1,
                      core::CudaBlockSize block=core::kDefaultCudaBlock);
std::string cudaGeometryLabel(core::CudaBlockSize block,const CudaUiInfo& info);
}

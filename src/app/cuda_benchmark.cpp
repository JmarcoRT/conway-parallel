#include "app/cuda_benchmark.hpp"
#ifdef CONWAY_HAS_CUDA
#include "core/engines/cuda.hpp"
#endif
#include <chrono>
#include <memory>

namespace conway::app {
void validateCudaBenchmark(std::size_t width,std::size_t height,core::CudaBlockSize block) {
    (void)core::planCudaLaunch(width,height,block);
#ifdef CONWAY_HAS_CUDA
    (void)core::CudaBackend::launchConfiguration(width,height,block);
#else
    throw CudaBenchmarkError("CUDA no disponible: build sin CUDA (CONWAY_ENABLE_CUDA=OFF)",true);
#endif
}
CudaBenchmarkRun runCudaBenchmark(const InputData& input,int generations,core::CudaBlockSize block) {
    if(generations<1)throw std::invalid_argument("CUDA benchmark requires positive generations");
    validateCudaBenchmark(input.width,input.height,block);
#ifdef CONWAY_HAS_CUDA
    using Clock=std::chrono::steady_clock;
    auto ms=[](auto start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
    // Property queries precede the operation timer; snapshots/equivalence never
    // occur per generation. The same kernel is shared with normal simulation.
    const auto hardware=core::CudaBackend::hardwareInfo();
    const auto geometry=core::planCudaLaunch(input.width,input.height,block);
    std::unique_ptr<core::CudaBackend> backend;
    bool constructing=false;
    const auto totalStart=Clock::now();
    try {
        constructing=true;backend=std::make_unique<core::CudaBackend>(input,0,0,block);constructing=false;
        const double prepare=ms(totalStart);
        const auto timing=backend->runBatch(generations);
        const auto snapshotStart=Clock::now();auto final=backend->snapshot();const double snapshot=ms(snapshotStart);
        const bool clean=backend->close();backend.reset();
        const double total=ms(totalStart);
        if(!clean)throw CudaBenchmarkError("CUDA: no se pudo confirmar la liberación de recursos",false);
        CudaMeasurement observed{timing.deviceMs,prepare,snapshot,total,
            geometry.gridX,geometry.gridY,geometry.threadsPerBlock,geometry.outsidePositions,hardware.totalMemoryBytes,
            hardware.device,hardware.major,hardware.minor,hardware.runtimeVersion,hardware.driverApiVersion,
            hardware.name,hardware.nvccVersion,true};
        return {std::move(final),timing.wallMs,std::move(observed)};
    } catch(const CudaBenchmarkError&) {throw;}
      catch(const std::exception& error) {
        const bool clean=backend?backend->close():!constructing;
        throw CudaBenchmarkError(std::string("CUDA benchmark: ")+error.what(),clean);
    }
#else
    (void)input;throw CudaBenchmarkError("CUDA no disponible: build sin CUDA",true);
#endif
}
}

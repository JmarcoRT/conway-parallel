#include "core/engines/cuda.hpp"
#include "core/game.hpp"
#include "game_test_access.hpp"
#include <cuda_runtime.h>
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace conway;
namespace {
int allocations=0,transfers=0;
bool observe=false,failRecord=false,failDestroy=false;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void same(const core::GameState& state,const Game& game) {
    const auto& expected=detail::GameTestAccess::state(game);
    require(state.current==expected.current && state.next==expected.next && state.generation==expected.generation && state.aliveCount==expected.aliveCount,"batch state/buffers differ from sequential");
}
}
#ifdef CONWAY_CUDA_BATCH_WRAP
extern "C" cudaError_t __real_cudaMalloc(void**,std::size_t);
extern "C" cudaError_t __wrap_cudaMalloc(void** pointer,std::size_t size) {if(observe)++allocations;return __real_cudaMalloc(pointer,size);}
extern "C" cudaError_t __real_cudaMemcpyAsync(void*,const void*,std::size_t,cudaMemcpyKind,cudaStream_t);
extern "C" cudaError_t __wrap_cudaMemcpyAsync(void* dst,const void* src,std::size_t size,cudaMemcpyKind kind,cudaStream_t stream) {if(observe)++transfers;return __real_cudaMemcpyAsync(dst,src,size,kind,stream);}
extern "C" cudaError_t __real_cudaEventRecord(cudaEvent_t,cudaStream_t);
extern "C" cudaError_t __wrap_cudaEventRecord(cudaEvent_t event,cudaStream_t stream) {if(failRecord) {failRecord=false;return cudaErrorInvalidValue;}return __real_cudaEventRecord(event,stream);}
extern "C" cudaError_t __real_cudaEventDestroy(cudaEvent_t);
extern "C" cudaError_t __wrap_cudaEventDestroy(cudaEvent_t event) {if(failDestroy) {failDestroy=false;return cudaErrorInvalidValue;}return __real_cudaEventDestroy(event);}
#endif
int main() {
    try {
        auto available=core::cuda_device_status();if(available.availability==core::CudaAvailability::NoDevice)return 77;
        require(available.availability==core::CudaAvailability::Available,available.diagnostic.c_str());
        for(auto block:{core::kCudaBlock8x8,core::kCudaBlock16x16,core::kCudaBlock32x8})
        for(auto [width,height]:{std::pair{1,1},{1,17},{17,1},{17,17},{35,19},{1024,513}}) {
            InputData input{static_cast<std::size_t>(width),static_cast<std::size_t>(height),std::vector<std::uint8_t>(width*height)};
            for(std::size_t i=0;i<input.cells.size();++i)input.cells[i]=(i*13+i/width)%7<3;
            Game oracle(input);core::CudaBackend batch(input,0,0,block);
            for(int count:{1,2,7}) {
                for(int i=0;i<count;++i)oracle.step();
                allocations=transfers=0;observe=true;const auto timing=batch.runBatch(count);observe=false;
                require(std::isfinite(timing.wallMs) && timing.wallMs>=0 && std::isfinite(timing.deviceMs) && timing.deviceMs>=0,"invalid timing");
                require(!allocations && !transfers,"resident batch allocated/copied board memory");same(batch.snapshot(),oracle);
            }
            require(batch.close() && batch.close(),"checked close failed or is not idempotent");
        }
#ifdef CONWAY_CUDA_BATCH_WRAP
        core::CudaBackend failedCleanup(InputData{1,1,{1}});failDestroy=true;
        bool cleanupRejected=false;try {failedCleanup.runBatch(1);}catch(const core::CudaError&) {cleanupRejected=true;}
        require(cleanupRejected && !failedCleanup.close(),"event cleanup failure was reported as confirmed");
        core::CudaBackend failing(InputData{1,1,{1}});failRecord=true;
        bool rejected=false;try {failing.runBatch(2);}catch(const core::CudaError&) {rejected=true;}
        require(rejected,"event failure was not reported");rejected=false;
        try {failing.snapshot();}catch(const std::logic_error&) {rejected=true;}
        require(rejected && failing.close(),"failed backend published state or could not release resources");
#endif
        InputData initial{3,3,{0,0,0,1,1,1,0,0,0}};Game gpu(initial),cpu(initial);
        gpu.setEngine(Engine::CUDA);gpu.setCudaBlockSize(core::kCudaBlock8x8);gpu.step();cpu.step();
        require(gpu.releaseCudaResources() && detail::sameGameBuffers(gpu,cpu),"suspending GPU changed confirmed CPU state");
        gpu.step();cpu.step();require(detail::sameGameBuffers(gpu,cpu) && gpu.cudaBlockSize()==core::kCudaBlock8x8,"GPU did not reconstruct after suspension");
        std::cout<<"PASS resident CUDA batches, exact buffers, timings, no board copies/allocations and event failure cleanup\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

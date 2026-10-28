// Test-only GNU linker wrapping: real allocations/kernels, one injected API
// error at a time. No production switches, VRAM exhaustion or device reset.
#include "core/engines/cuda.hpp"
#include <cuda_runtime_api.h>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
enum class Call { None, Allocate, CreateStream, Copy, Clear, Synchronize, LaunchCheck };
Call fault = Call::None;
int remaining{};
std::set<void*> allocations;
std::set<cudaStream_t> streams;
bool pendingTransfer{}, cleanupError{};

bool inject(Call call) {
    if (fault != call || --remaining != 0) return false;
    fault = Call::None; // Cleanup and the next backend use the real runtime.
    return true;
}
void arm(Call call, int ordinal = 1) {
    fault = call;
    remaining = ordinal;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void released() {
    require(allocations.empty() && streams.empty(), "Leaked backend allocation/stream");
    require(!pendingTransfer && !cleanupError, "Cleanup did not drain host transfers or double-freed resources");
    require(fault == Call::None, "Fault injection did not reach the selected API call");
}
template<class Action>
void cuda_error(Action action, cudaError_t expected, const char* operation) {
    try { action(); }
    catch (const conway::core::CudaError& error) {
        require(error.code() == static_cast<int>(expected), "CUDA error code was lost");
        require(std::string(error.what()).find(operation) != std::string::npos, "CUDA operation diagnostic was lost");
        return;
    }
    throw std::runtime_error("Injected CUDA error was not reported");
}
template<class Action>
void invalidated(Action action) {
    try { action(); }
    catch (const std::logic_error&) { return; }
    throw std::runtime_error("Failed backend remained usable");
}
const InputData input{3, 3, {0,0,0,1,1,1,0,0,0}};
} // namespace

extern "C" {
cudaError_t __real_cudaMalloc(void**, std::size_t);
cudaError_t __real_cudaFree(void*);
cudaError_t __real_cudaStreamCreateWithFlags(cudaStream_t*, unsigned);
cudaError_t __real_cudaStreamDestroy(cudaStream_t);
cudaError_t __real_cudaMemcpyAsync(void*, const void*, std::size_t, cudaMemcpyKind, cudaStream_t);
cudaError_t __real_cudaMemsetAsync(void*, int, std::size_t, cudaStream_t);
cudaError_t __real_cudaStreamSynchronize(cudaStream_t);
cudaError_t __real_cudaGetLastError();

cudaError_t __wrap_cudaMalloc(void** pointer, std::size_t bytes) {
    if (inject(Call::Allocate)) return cudaErrorMemoryAllocation;
    const auto code = __real_cudaMalloc(pointer, bytes);
    if (code == cudaSuccess) allocations.insert(*pointer);
    return code;
}
cudaError_t __wrap_cudaFree(void* pointer) {
    if (pendingTransfer || !allocations.contains(pointer)) cleanupError = true;
    const auto code = __real_cudaFree(pointer);
    if (code == cudaSuccess) allocations.erase(pointer);
    else cleanupError = true;
    return code;
}
cudaError_t __wrap_cudaStreamCreateWithFlags(cudaStream_t* stream, unsigned flags) {
    if (inject(Call::CreateStream)) return cudaErrorUnknown;
    const auto code = __real_cudaStreamCreateWithFlags(stream, flags);
    if (code == cudaSuccess) streams.insert(*stream);
    return code;
}
cudaError_t __wrap_cudaStreamDestroy(cudaStream_t stream) {
    if (pendingTransfer || !streams.contains(stream)) cleanupError = true;
    const auto code = __real_cudaStreamDestroy(stream);
    if (code == cudaSuccess) streams.erase(stream);
    else cleanupError = true;
    return code;
}
cudaError_t __wrap_cudaMemcpyAsync(void* to, const void* from, std::size_t bytes,
                                 cudaMemcpyKind kind, cudaStream_t stream) {
    if (inject(Call::Copy)) return cudaErrorUnknown;
    const auto code = __real_cudaMemcpyAsync(to, from, bytes, kind, stream);
    if (code == cudaSuccess) pendingTransfer = true;
    return code;
}
cudaError_t __wrap_cudaMemsetAsync(void* to, int value, std::size_t bytes, cudaStream_t stream) {
    if (inject(Call::Clear)) return cudaErrorUnknown;
    return __real_cudaMemsetAsync(to, value, bytes, stream);
}
cudaError_t __wrap_cudaStreamSynchronize(cudaStream_t stream) {
    // Complete actual GPU work before synthesizing an execution error: no real
    // illegal access/context poisoning is needed to exercise exception handling.
    const auto code = __real_cudaStreamSynchronize(stream);
    if (code == cudaSuccess) pendingTransfer = false;
    if (inject(Call::Synchronize)) return cudaErrorUnknown;
    return code;
}
cudaError_t __wrap_cudaGetLastError() {
    const auto code = __real_cudaGetLastError();
    if (inject(Call::LaunchCheck)) return cudaErrorUnknown;
    return code;
}
}

int main() {
    using namespace conway::core;
    try {
        const auto status = cuda_device_status();
        if (status.availability == CudaAvailability::NoDevice) return 77;
        require(status.availability == CudaAvailability::Available, status.diagnostic.c_str());
        for (int ordinal = 1; ordinal <= 3; ++ordinal) {
            arm(Call::Allocate, ordinal);
            cuda_error([] { CudaBackend backend(input); }, cudaErrorMemoryAllocation, "cudaMalloc");
            released();
        }
        for (auto call : {Call::CreateStream, Call::Clear, Call::Synchronize}) {
            arm(call);
            cuda_error([] { CudaBackend backend(input); }, cudaErrorUnknown,
                       call == Call::CreateStream ? "cudaStreamCreate" :
                       call == Call::Clear ? "initial next/halo clear" : "initialization synchronization");
            released();
        }
        for (int ordinal : {1, 2}) {
            arm(Call::Copy, ordinal);
            cuda_error([] { CudaBackend backend(input); }, cudaErrorUnknown,
                       ordinal == 1 ? "initial board upload" : "initial population upload");
            released();
        }
        for (auto call : {Call::Clear, Call::LaunchCheck, Call::Synchronize}) {
            {
                CudaBackend backend(input);
                const auto saved = backend.snapshot();
                arm(call);
                cuda_error([&] { backend.step(); }, cudaErrorUnknown,
                           call == Call::Clear ? "population clear" :
                           call == Call::LaunchCheck ? "evolve kernel launch" : "evolve kernel execution");
                invalidated([&] { backend.step(); });
                invalidated([&] { (void)backend.snapshot(); });
                require(saved.generation == 0 && saved.aliveCount == 3, "Failure changed an earlier CPU snapshot");
            }
            released();
        }
        for (int ordinal : {1, 2, 3, 4}) {
            {
                CudaBackend backend(input);
                arm(ordinal == 4 ? Call::Synchronize : Call::Copy, ordinal == 4 ? 1 : ordinal);
                cuda_error([&] { (void)backend.snapshot(); }, cudaErrorUnknown,
                           ordinal == 1 ? "current snapshot" : ordinal == 2 ? "next snapshot" :
                           ordinal == 3 ? "population snapshot" : "snapshot synchronization");
                invalidated([&] { backend.step(); });
                invalidated([&] { (void)backend.snapshot(); });
            }
            released();
        }
        {
            CudaBackend healthy(input), destination(input);
            healthy.step();
            CudaBackend moved(std::move(healthy));
            destination = std::move(moved); // Must release destination's old resources.
            destination = std::move(destination);
            require(destination.snapshot().generation == 1, "Recovery/move lost state");
            require(allocations.size() == 3 && streams.size() == 1, "Move assignment leaked its old resources");
        }
        released();
        std::cout << "PASS 15 injected failures: partial construction, host-transfer drain, launch/execution diagnostics, invalidation and resource release\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

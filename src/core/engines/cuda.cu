#include "core/engines/cuda.hpp"
#include "core/detail/bs23_rule.hpp"
#include "input/validated_dimensions.hpp"
#include <cuda_runtime.h>

#include <algorithm>
#include <utility>

namespace conway::core {
namespace {
void check(cudaError_t code, const char* operation) {
    if (code != cudaSuccess)
        throw CudaError(static_cast<int>(code), std::string(operation) + ": "
                        + cudaGetErrorName(code) + " (" + cudaGetErrorString(code) + ")");
}

class DeviceSelection {
public:
    explicit DeviceSelection(int device) {
        check(cudaGetDevice(&previous_), "cudaGetDevice");
        if (previous_ != device) check(cudaSetDevice(device), "cudaSetDevice");
    }
    ~DeviceSelection() { (void)cudaSetDevice(previous_); }
    DeviceSelection(const DeviceSelection&) = delete;
    DeviceSelection& operator=(const DeviceSelection&) = delete;
private:
    int previous_{};
};

// Destructors cannot report errors. Release in the allocation's device context,
// best effort if the driver/context has already been lost. Never reset a device.
template<class Release>
void release_on_device(int device, Release release) noexcept {
    int previous = -1;
    (void)cudaGetDevice(&previous);
    if (cudaSetDevice(device) == cudaSuccess) release();
    if (previous >= 0) (void)cudaSetDevice(previous);
}

template<class T>
class DeviceBuffer {
public:
    DeviceBuffer() = default;
    ~DeviceBuffer() {
        if (data) release_on_device(device_, [this] { (void)cudaFree(data); });
    }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    void allocate(std::size_t count, int device) {
        device_ = device;
        check(cudaMalloc(reinterpret_cast<void**>(&data), count * sizeof(T)), "cudaMalloc");
    }
    T* data{};
private:
    int device_{};
};

class Stream {
public:
    Stream() = default;
    ~Stream() {
        if (value) release_on_device(device_, [this] { (void)cudaStreamDestroy(value); });
    }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    void create(int device) {
        device_ = device;
        check(cudaStreamCreateWithFlags(&value, cudaStreamNonBlocking), "cudaStreamCreateWithFlags");
    }
    cudaStream_t value{};
private:
    int device_{};
};

// Keep host staging storage alive until queued transfers finish, also when a
// later CUDA call throws. Construct after those host objects, inside selection.
class StreamCompletion {
public:
    explicit StreamCompletion(cudaStream_t stream) : stream_(stream) {}
    ~StreamCompletion() { if (pending_) (void)cudaStreamSynchronize(stream_); }
    void finish(const char* operation) {
        check(cudaStreamSynchronize(stream_), operation);
        pending_ = false;
    }
private:
    cudaStream_t stream_;
    bool pending_{true};
};

constexpr unsigned maxBlockThreads = 256; // Largest supported preset.
__global__ void evolve(const std::uint8_t* current, std::uint8_t* next,
                       std::size_t width, std::size_t height, std::size_t stride,
                       unsigned long long* population) {
    const auto col = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto row = static_cast<std::size_t>(blockIdx.y) * blockDim.y + threadIdx.y;
    const unsigned lane = threadIdx.y * blockDim.x + threadIdx.x;
    const unsigned threads = blockDim.x * blockDim.y;
    unsigned long long alive = 0;
    if (row < height && col < width) {
        const auto after = detail::evaluate_cell(current, stride, row + 1, col + 1).after;
        next[(row + 1) * stride + col + 1] = after;
        alive = after;
    }
    // Out-of-board threads still participate in every block barrier/reduction.
    __shared__ unsigned long long partial[maxBlockThreads];
    partial[lane] = alive;
    __syncthreads();
    for (unsigned offset = threads / 2; offset; offset /= 2) {
        if (lane < offset) partial[lane] += partial[lane + offset];
        __syncthreads();
    }
    if (lane == 0) atomicAdd(population, partial[0]);
}
} // namespace

CudaError::CudaError(int code, const std::string& message) : std::runtime_error(message), code_(code) {}

CudaDeviceStatus cuda_device_status() {
    int count = 0;
    const auto code = cudaGetDeviceCount(&count);
    if (code == cudaSuccess && count > 0)
        return {CudaAvailability::Available, count, 0, "CUDA device available"};
    if (code == cudaErrorNoDevice || (code == cudaSuccess && count == 0))
        return {CudaAvailability::NoDevice, 0, static_cast<int>(cudaErrorNoDevice), "No visible CUDA devices"};
    return {code == cudaErrorInsufficientDriver ? CudaAvailability::DriverUnavailable : CudaAvailability::RuntimeError,
            0, static_cast<int>(code), std::string(cudaGetErrorName(code)) + ": " + cudaGetErrorString(code)};
}

struct CudaBackend::Impl {
    int device;
    std::size_t width, height, stride, cells, padded, generation;
    CudaLaunchGeometry launch;
    bool failed{};
    DeviceBuffer<std::uint8_t> current, next;
    DeviceBuffer<unsigned long long> population;
    Stream stream;

    Impl(const InputData& input, std::size_t initialGeneration, int requestedDevice, CudaBlockSize block)
        : device(requestedDevice), width(input.width), height(input.height), generation(initialGeneration) {
        // Match Game's validation before touching CUDA or allocating either board.
        const auto shape = conway::input::detail::validateDimensions(width, height);
        if (input.cells.size() != shape.cells)
            throw std::invalid_argument("La cantidad de celulas no coincide con las dimensiones.");
        if (std::any_of(input.cells.begin(), input.cells.end(), [](auto value) { return value > 1; }))
            throw std::invalid_argument("El tablero solo puede contener 0 y 1.");
        stride = shape.stride;
        cells = shape.cells;
        padded = shape.padded;
        launch = CudaBackend::launchConfiguration(width, height, block, device);
        DeviceSelection selected(device);
        std::vector<std::uint8_t> initial(padded, 0);
        unsigned long long alive = 0;
        for (std::size_t row = 0; row < height; ++row)
            for (std::size_t col = 0; col < width; ++col) {
                const auto value = input.cells[row * width + col];
                initial[(row + 1) * stride + col + 1] = value;
                alive += value;
            }
        current.allocate(padded, device);
        next.allocate(padded, device);
        population.allocate(1, device);
        stream.create(device);
        StreamCompletion completion(stream.value);
        check(cudaMemcpyAsync(current.data, initial.data(), padded, cudaMemcpyHostToDevice, stream.value), "initial board upload");
        check(cudaMemsetAsync(next.data, 0, padded, stream.value), "initial next/halo clear");
        check(cudaMemcpyAsync(population.data, &alive, sizeof(alive), cudaMemcpyHostToDevice, stream.value), "initial population upload");
        completion.finish("initialization synchronization");
    }

    void usable() const {
        if (failed) throw std::logic_error("CUDA backend is unusable after an execution/transfer error; reconstruct it");
    }
};

CudaLaunchGeometry CudaBackend::launchConfiguration(std::size_t width, std::size_t height,
                                                     CudaBlockSize block, int device) {
    const auto launch = planCudaLaunch(width, height, block);
    const auto status = cuda_device_status();
    if (status.availability != CudaAvailability::Available)
        throw CudaError(status.errorCode, status.diagnostic);
    if (device < 0 || device >= status.deviceCount)
        throw std::invalid_argument("CUDA device index out of range");
    DeviceSelection selected(device);
    cudaDeviceProp properties{};
    check(cudaGetDeviceProperties(&properties, device), "cudaGetDeviceProperties");
    cudaFuncAttributes kernel{};
    check(cudaFuncGetAttributes(&kernel, evolve), "cudaFuncGetAttributes");
    if (properties.maxThreadsDim[0] < static_cast<int>(block.x) ||
        properties.maxThreadsDim[1] < static_cast<int>(block.y) || properties.maxThreadsDim[2] < 1 ||
        properties.maxThreadsPerBlock < static_cast<int>(launch.threadsPerBlock) ||
        kernel.maxThreadsPerBlock < static_cast<int>(launch.threadsPerBlock))
        throw std::invalid_argument("CUDA block exceeds device/kernel limits");
    if (properties.maxGridSize[0] < 1 || properties.maxGridSize[1] < 1 || properties.maxGridSize[2] < 1 ||
        launch.gridX > static_cast<std::size_t>(properties.maxGridSize[0]) ||
        launch.gridY > static_cast<std::size_t>(properties.maxGridSize[1]))
        throw std::invalid_argument("CUDA grid exceeds device limits");
    return launch;
}

std::string CudaBackend::deviceName(int device) {
    const auto status = cuda_device_status();
    if (status.availability != CudaAvailability::Available)
        throw CudaError(status.errorCode, status.diagnostic);
    if (device < 0 || device >= status.deviceCount)
        throw std::invalid_argument("CUDA device index out of range");
    DeviceSelection selected(device);
    cudaDeviceProp properties{};
    check(cudaGetDeviceProperties(&properties, device), "cudaGetDeviceProperties");
    return properties.name;
}

CudaBackend::CudaBackend(const InputData& input, std::size_t generation, int device, CudaBlockSize block)
    : impl_(std::make_unique<Impl>(input, generation, device, block)) {}

CudaLaunchGeometry CudaBackend::launchConfiguration() const {
    if (!impl_) throw std::logic_error("Moved-from CUDA backend");
    impl_->usable();
    return impl_->launch;
}
void CudaBackend::setBlockSize(CudaBlockSize block) {
    if (!impl_) throw std::logic_error("Moved-from CUDA backend");
    impl_->usable();
    const auto candidate = launchConfiguration(impl_->width, impl_->height, block, impl_->device);
    impl_->launch = candidate; // No buffer, population, stream or generation mutation.
}
CudaBackend::~CudaBackend() = default;
CudaBackend::CudaBackend(CudaBackend&&) noexcept = default;
CudaBackend& CudaBackend::operator=(CudaBackend&&) noexcept = default;

void CudaBackend::step() {
    if (!impl_) throw std::logic_error("Moved-from CUDA backend");
    auto& state = *impl_;
    state.usable();
    try {
        DeviceSelection selected(state.device);
        check(cudaMemsetAsync(state.population.data, 0, sizeof(unsigned long long), state.stream.value), "population clear");
        const auto& launch = state.launch;
        const dim3 grid(static_cast<unsigned>(launch.gridX), static_cast<unsigned>(launch.gridY));
        const dim3 block(launch.block.x, launch.block.y);
        evolve<<<grid, block, 0, state.stream.value>>>(state.current.data, state.next.data,
                state.width, state.height, state.stride, state.population.data);
        check(cudaGetLastError(), "evolve kernel launch");
        check(cudaStreamSynchronize(state.stream.value), "evolve kernel execution");
        std::swap(state.current.data, state.next.data);
        ++state.generation;
    } catch (...) {
        state.failed = true;
        throw;
    }
}

GameState CudaBackend::snapshot() const {
    if (!impl_) throw std::logic_error("Moved-from CUDA backend");
    auto& state = *impl_;
    state.usable();
    GameState result{state.width, state.height, state.stride,
                     std::vector<std::uint8_t>(state.padded), std::vector<std::uint8_t>(state.padded),
                     state.generation, 0};
    try {
        DeviceSelection selected(state.device);
        unsigned long long alive{};
        StreamCompletion completion(state.stream.value);
        check(cudaMemcpyAsync(result.current.data(), state.current.data, state.padded, cudaMemcpyDeviceToHost, state.stream.value), "current snapshot");
        check(cudaMemcpyAsync(result.next.data(), state.next.data, state.padded, cudaMemcpyDeviceToHost, state.stream.value), "next snapshot");
        check(cudaMemcpyAsync(&alive, state.population.data, sizeof(alive), cudaMemcpyDeviceToHost, state.stream.value), "population snapshot");
        completion.finish("snapshot synchronization");
        if (alive > state.cells) throw std::logic_error("Invalid CUDA population");
        result.aliveCount = static_cast<std::size_t>(alive);
    } catch (...) {
        state.failed = true;
        throw;
    }
    return result;
}
} // namespace conway::core

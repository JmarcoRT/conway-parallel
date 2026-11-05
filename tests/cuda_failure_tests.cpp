// Test-only GNU linker wrapping: real allocations/kernels, one injected API
// error at a time. No production switches, VRAM exhaustion or device reset.
#include "core/engines/cuda.hpp"
#include "core/game.hpp"
#include "app/cuda_info.hpp"
#include "app/cuda_didactic_session.hpp"
#include "ui/cuda_didactic_view.hpp"
#include <ftxui/screen/screen.hpp>
#include "app/simulation.hpp"
#include "game_test_access.hpp"
#include <cstdlib>
#include <new>
#include <chrono>
#include <thread>
#include <cuda_runtime_api.h>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace { bool failAllocation{}, failAfterSync{}; int allocationOrdinal=1; bool failAfterAttributes{}; }
// Confined to this executable: force CPU snapshot allocation failure after GPU step.
void* operator new(std::size_t bytes) {
    if (failAllocation && --allocationOrdinal==0) { failAllocation=false; allocationOrdinal=1; throw std::bad_alloc(); }
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
namespace {
enum class Call { None, Allocate, CreateStream, Copy, Clear, Synchronize, LaunchCheck, Properties, KernelAttributes };
enum class Limit { None, BlockX, BlockY, BlockZ, Threads, GridX, GridY, GridZ, KernelThreads };
Limit limit=Limit::None;
std::size_t propertyCalls=0, allocationCalls=0, queryCalls=0;
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
cudaError_t __real_cudaGetDeviceCount(int*);
cudaError_t __real_cudaGetDevice(int*);
cudaError_t __wrap_cudaGetDeviceCount(int* count) {++queryCalls;return __real_cudaGetDeviceCount(count);}
cudaError_t __wrap_cudaGetDevice(int* device) {++queryCalls;return __real_cudaGetDevice(device);}
cudaError_t __real_cudaGetDeviceProperties_v2(cudaDeviceProp*, int);
cudaError_t __real_cudaFuncGetAttributes(cudaFuncAttributes*, const void*);
cudaError_t __wrap_cudaGetDeviceProperties_v2(cudaDeviceProp* p, int device) {
    ++propertyCalls;++queryCalls;
    if (inject(Call::Properties)) return cudaErrorUnknown;
    const auto code=__real_cudaGetDeviceProperties_v2(p,device);
    if (code != cudaSuccess) return code;
    switch (limit) {
        case Limit::BlockX: p->maxThreadsDim[0]=7; break;
        case Limit::BlockY: p->maxThreadsDim[1]=7; break;
        case Limit::BlockZ: p->maxThreadsDim[2]=0; break;
        case Limit::Threads: p->maxThreadsPerBlock=63; break;
        case Limit::GridX: p->maxGridSize[0]=3; break;
        case Limit::GridY: p->maxGridSize[1]=4; break;
        case Limit::GridZ: p->maxGridSize[2]=0; break;
        default: break;
    }
    if (limit != Limit::KernelThreads) limit=Limit::None;
    return code;
}
cudaError_t __wrap_cudaFuncGetAttributes(cudaFuncAttributes* p, const void* kernel) {
    ++queryCalls;
    if (inject(Call::KernelAttributes)) return cudaErrorUnknown;
    const auto code=__real_cudaFuncGetAttributes(p,kernel);
    if (code==cudaSuccess && limit==Limit::KernelThreads) { p->maxThreadsPerBlock=63; limit=Limit::None; }
    if (code==cudaSuccess && failAfterAttributes) {failAfterAttributes=false;failAllocation=true;}
    return code;
}

cudaError_t __wrap_cudaMalloc(void** pointer, std::size_t bytes) {
    ++allocationCalls;
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
    if (failAfterSync) { failAfterSync = false; failAllocation = true; }
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
        // Game must retain CPU state and discard an advanced GPU on every error.
        auto same = [](const conway::Game& a, const conway::Game& b) {
            require(conway::detail::sameGameBuffers(a,b), "Game buffers/halos/counters changed");
            require(a.generation() == b.generation() && a.aliveCount() == b.aliveCount(), "Game counters published on failure");
            for (std::size_t r=0;r<a.height();++r)
                for (std::size_t c=0;c<a.width();++c)
                    require(a.isAlive(r,c) == b.isAlive(r,c), "Game cells published on failure");
        };
        for (int scenario=0;scenario<11;++scenario) {
            {
                conway::Game game(input), ref(input);
                game.setEngine(conway::Engine::CUDA);
                game.step(); ref.step();
                if (scenario == 0) {
                    // Copy and assignment must not access/allocate GPU resources.
                    arm(Call::Allocate);
                    conway::Game copy(game), assigned(input); assigned=game;
                    same(copy,ref); same(assigned,ref);
                    require(allocations.size() == 3 && streams.size() == 1, "Game copies allocated GPU resources");
                    cuda_error([&] { copy.step(); }, cudaErrorMemoryAllocation, "cudaMalloc");
                    same(copy,ref); copy.step();
                } else {
                    if (scenario == 1) arm(Call::Clear);
                    if (scenario == 2) arm(Call::LaunchCheck);
                    if (scenario == 3) arm(Call::Synchronize);
                    if (scenario >= 4 && scenario <= 6) arm(Call::Copy, scenario-3);
                    if (scenario == 7) arm(Call::Synchronize, 2);
                    if (scenario == 8 || scenario == 10) { failAfterSync=true; allocationOrdinal=scenario==10 ? 2 : 1; }
                    if (scenario == 9) { game.setEngine(conway::Engine::Sequential); game.setEngine(conway::Engine::CUDA); arm(Call::Allocate); }
                    bool failed=false;
                    try { game.step(); }
                    catch (const std::bad_alloc&) { require(scenario == 8 || scenario == 10, "Unexpected host allocation failure"); failed=true; }
                    catch (const CudaError&) { require(scenario != 8 && scenario != 10, "Expected CPU bad_alloc"); failed=true; }
                    require(failed, "Game did not report failure");
                    require(!failAfterSync && !failAllocation, "Host allocation failure not reached");
                    same(game,ref);
                    require(allocations.empty() && streams.empty(), "Game retained failed GPU cache");
                    game.step(); ref.step(); same(game,ref);
                }
            }
            released();
        }
        {
            conway::Game source(input), destination(input);
            source.setEngine(conway::Engine::CUDA); source.setCudaBlockSize(kCudaBlock32x8); source.step();
            const auto saved = destination;
            failAllocation=true;
            bool failed=false;
            try { destination=source; } catch (const std::bad_alloc&) { failed=true; }
            require(failed && !failAllocation, "Copy allocation failure not reached");
            same(destination,saved);
            require(destination.engine() == saved.engine(), "Failed assignment changed engine");
            require(destination.cudaBlockSize() == saved.cudaBlockSize(), "Failed assignment changed block configuration");
        }
        released();
        {
            InputData board{37,53,std::vector<std::uint8_t>(37*53,0)};
            board.cells[38]=board.cells[39]=board.cells[40]=1;
            conway::Game game(board), ref(board);
            game.setEngine(conway::Engine::CUDA); game.step(); ref.step();
            const auto original=game.cudaLaunchConfiguration();
            for (auto rejectedLimit : {Limit::BlockX,Limit::BlockY,Limit::BlockZ,Limit::Threads,
                                       Limit::GridX,Limit::GridY,Limit::GridZ,Limit::KernelThreads}) {
                limit=rejectedLimit;
                bool rejected=false;
                try { game.setCudaBlockSize(kCudaBlock8x8); }
                catch (const std::invalid_argument&) { rejected=true; }
                require(rejected && limit==Limit::None, "Device/kernel limit was not checked");
                require(game.cudaLaunchConfiguration()==original && game.cudaBlockSize()==original.block,
                        "Rejected configuration was published");
                same(game,ref);
                require(allocations.size()==3 && streams.size()==1, "Rejected configuration discarded GPU resources");
            }
            for (auto call : {Call::Properties,Call::KernelAttributes}) {
                arm(call);
                cuda_error([&] { game.setCudaBlockSize(kCudaBlock8x8); },cudaErrorUnknown,
                           call==Call::Properties ? "cudaGetDeviceProperties" : "cudaFuncGetAttributes");
                same(game,ref); require(game.cudaLaunchConfiguration()==original,"Query error changed geometry");
            }
            arm(Call::Allocate);
            game.setCudaBlockSize(kCudaBlock8x8); same(game,ref);
            game.step(); ref.step(); same(game,ref);
            require(fault==Call::Allocate && allocations.size()==3 && streams.size()==1,
                    "Configuration change rebuilt resident resources");
            fault=Call::None;
        }
        released();
        {
            conway::Game cold(input), saved(cold);
            arm(Call::Properties);
            cuda_error([&] { cold.setCudaBlockSize(kCudaBlock8x8); },cudaErrorUnknown,"cudaGetDeviceProperties");
            same(cold,saved);
            require(cold.cudaBlockSize()==kDefaultCudaBlock,"Cold configuration changed on query error");
        }
        released();
        // Real grid boundaries, queried without allocating enormous boards.
        {
            cudaDeviceProp properties{};
            require(cudaGetDeviceProperties(&properties,0)==cudaSuccess,"Cannot inspect real device limits");
            const auto maximumX=static_cast<std::size_t>(properties.maxGridSize[0])*8;
            const auto maximumY=static_cast<std::size_t>(properties.maxGridSize[1])*8;
            require(CudaBackend::launchConfiguration(maximumX,1,kCudaBlock8x8).gridX==
                    static_cast<std::size_t>(properties.maxGridSize[0]),"Valid grid x boundary rejected");
            require(CudaBackend::launchConfiguration(1,maximumY,kCudaBlock8x8).gridY==
                    static_cast<std::size_t>(properties.maxGridSize[1]),"Valid grid y boundary rejected");
            for (auto [w,h] : {std::pair{maximumX+1,std::size_t{1}}, {std::size_t{1},maximumY+1}}) {
                bool rejected=false;
                try { (void)CudaBackend::launchConfiguration(w,h,kCudaBlock8x8); }
                catch (const std::invalid_argument&) { rejected=true; }
                require(rejected,"Real grid boundary exceeded without rejection");
            }
        }
        released();
        {
            arm(Call::Allocate);
            auto status=conway::app::inspectCuda(3,3);
            require(!status.available && status.diagnostic.find("inicialización")!=std::string::npos,
                    "Initialization error not visible to UI");
        }
        released();
        {
            conway::app::SimulationSession session(input,conway::Engine::CUDA);
            session.step();const auto saved=session.game();
            arm(Call::LaunchCheck);session.toggleRunning();session.step();
            require(!session.error().empty() && !session.isRunning() && session.engine()==conway::Engine::CUDA,
                    "CUDA session error did not pause/report, or silently changed engine");
            same(session.game(),saved);
            session.recover();require(session.error().empty(),"Session retry failed");
            session.step();auto expected=saved;expected.setEngine(conway::Engine::Sequential);expected.step();
            same(session.game(),expected);
        }
        released();
        {
            // Engine transitions close the MPI worker before configuring a local
            // CUDA engine. Fail that asynchronous configuration, then retry.
            auto wait=[](conway::app::SimulationSession& session) {
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(session.busy() && std::chrono::steady_clock::now()<deadline) {
                    session.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                require(!session.busy(),"Transition worker timed out");
            };
            conway::app::SimulationSession session(input,conway::Engine::MPI);
            wait(session);require(session.error().empty(),session.error().c_str());
            const auto saved=session.game();
            failAfterAttributes=true;
            bool submissionFailed=false;
            try {session.configure(conway::Engine::CUDA,1,kCudaBlock32x8);}
            catch(const std::bad_alloc&) {submissionFailed=true;}
            require(submissionFailed && !failAfterAttributes && !failAllocation,
                    "Configuration submission allocation fault not reached");
            require(session.engine()==conway::Engine::MPI && session.game().cudaBlockSize()==saved.cudaBlockSize(),
                    "Failed submission changed engine/block");
            same(session.game(),saved);
            arm(Call::Properties,2); // First query validates UI candidate; second is in worker.
            session.configure(conway::Engine::CUDA,1,kCudaBlock32x8);
            wait(session);
            require(!session.error().empty(),"Asynchronous CUDA configuration fault not reached");
            same(session.game(),saved);
            session.recover();
            require(session.error().empty() && session.game().engine()==conway::Engine::CUDA,
                    "Recovery silently retained a CPU engine");
            arm(Call::LaunchCheck);session.step();
            require(!session.error().empty(),"Recovered session did not actually launch CUDA");
            same(session.game(),saved);
            session.recover();session.step();auto expected=saved;
            expected.setEngine(conway::Engine::Sequential);expected.step();same(session.game(),expected);
            session.close();wait(session);
        }
        released();
        {
            conway::app::SimulationSession session(input,conway::Engine::CUDA);
            conway::app::CudaDidacticController model;model.enter(session);
            session.step();model.refresh(session.game());const auto saved=session.game();
            const auto generation=model.frame().generation;
            const auto properties=propertyCalls,allocationsBefore=allocationCalls,queriesBefore=queryCalls;
            for(int i=0;i<5;++i)model.nextThread(session.game());
            require(queryCalls==queriesBefore && allocationCalls==allocationsBefore,"Exploration queried CUDA or allocated GPU");
            conway::ui::BoardViewport viewport;
            conway::ui::CudaDidacticPrimitives style{
                [](bool v,std::size_t width){return ftxui::text(std::string(width,v?'#':'.'));},
                [](std::string s){return ftxui::text(std::move(s));},false};
            const auto revision=model.revision();
            for(int i=0;i<5;++i) {
                auto screen=ftxui::Screen::Create(ftxui::Dimension::Fixed(80),ftxui::Dimension::Fixed(20));
                ftxui::Render(screen,conway::ui::renderCudaDidactic(session.game(),model.frame(),"RTX test","",viewport,false,0,true,80,20,style));
            }
            require(model.revision()==revision && propertyCalls==properties && queryCalls==queriesBefore && allocationCalls==allocationsBefore,
                    "Render queried CUDA, allocated GPU or rebuilt model detail");
            same(session.game(),saved);
            for(int scenario=0;scenario<3;++scenario) {
                if(scenario==0)arm(Call::LaunchCheck);
                else {failAfterSync=true;allocationOrdinal=scenario;}
                require(!model.advance(session),"Didactic failed advance reported success");
                same(session.game(),saved);
                require(model.frame().generation==generation && !session.error().empty() && !session.isRunning(),
                        "Didactic error lost confirmed frame/pause");
                require(model.recover(session) && session.game().generation()==saved.generation(),"Didactic recovery advanced generation");
            }
            require(model.advance(session),"Didactic real CUDA retry failed");
            auto expected=saved;expected.setEngine(conway::Engine::Sequential);expected.step();same(session.game(),expected);
        }
        released();
        std::cout << "PASS didactic const rendering without CUDA queries, launch/bad_alloc rollback and recovery\n";
        std::cout << "PASS UI initialization diagnosis and confirmed snapshot after session failure/recovery\n";
        std::cout << "PASS transactional block changes, device/kernel limits and real grid boundaries\n";
        std::cout << "PASS Game rollback and retry including CPU bad_alloc after GPU advancement\n";
        std::cout << "PASS 15 injected failures: partial construction, host-transfer drain, launch/execution diagnostics, invalidation and resource release\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

#include "core/game.hpp"
#include "game_test_access.hpp"
#ifdef CONWAY_HAS_CUDA
#include "core/engines/cuda.hpp"
#endif
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <type_traits>

using conway::Game;
using conway::Engine;
void require(bool ok) { if (!ok) throw std::runtime_error("Game CUDA contract failed"); }
void same(const Game& a, const Game& b) {
    require(conway::detail::sameGameBuffers(a,b));
    require(a.width() == b.width() && a.height() == b.height());
    require(a.generation() == b.generation() && a.aliveCount() == b.aliveCount());
    std::size_t alive = 0;
    for (std::size_t r = 0; r < a.height(); ++r)
        for (std::size_t c = 0; c < a.width(); ++c) {
            require(a.isAlive(r,c) == b.isAlive(r,c)); alive += a.isAlive(r,c);
        }
    require(alive == a.aliveCount());
}
struct Sink : conway::core::StepTraceSink {
    void onStepComplete(conway::core::StepTrace&&) override { throw std::runtime_error("Unexpected trace"); }
};
int main(int argc, char**) {
    try {
        static_assert(std::is_nothrow_move_constructible_v<Game>);
        static_assert(std::is_nothrow_move_assignable_v<Game>);
        const InputData input{3,3,{0,0,0,1,1,1,0,0,0}};
        require(Game(input).cudaBlockSize() == conway::core::kDefaultCudaBlock);
        bool unavailable = argc > 1;
#ifndef CONWAY_HAS_CUDA
        unavailable = true;
#endif
        if (unavailable) {
            Game game(input), saved(game);
            game.setEngine(Engine::CUDA);
            bool failed = false;
            try { game.step(); } catch (const std::exception&) { failed = true; }
            require(failed); same(game,saved); require(game.engine() == Engine::CUDA);
            Game copy(game); same(copy,saved);
            for (auto block : {conway::core::CudaBlockSize{0,8}, conway::core::kCudaBlock8x8}) {
                failed=false;
                try { game.setCudaBlockSize(block); } catch (const std::exception&) { failed=true; }
                require(failed); same(game,saved);
                require(game.cudaBlockSize() == conway::core::kDefaultCudaBlock);
            }
            failed=false;
            try { (void)game.cudaLaunchConfiguration(); } catch (const std::exception&) { failed=true; }
            require(failed); same(game,saved);
            game.setEngine(Engine::Sequential); game.step(); saved.step(); same(game,saved);
            std::cout << "PASS explicit unavailable CUDA and CPU recovery\n";
            return 0;
        }
#ifdef CONWAY_HAS_CUDA
        auto status = conway::core::cuda_device_status();
        if (status.availability == conway::core::CudaAvailability::NoDevice) return 77;
        require(status.availability == conway::core::CudaAvailability::Available);
        for (auto block : {conway::core::kCudaBlock8x8, conway::core::kCudaBlock16x16, conway::core::kCudaBlock32x8}) {
        for (auto [w,h] : {std::pair{1,1}, {1,64}, {64,1}, {3,3}, {37,53}, {200,150}}) {
            InputData in{static_cast<std::size_t>(w),static_cast<std::size_t>(h), std::vector<std::uint8_t>(w*h)};
            unsigned rng = 19;
            for (auto& v : in.cells) { rng = rng * 1664525u + 1013904223u; v = (rng >> 28) < 6; }
            Game ref(in), gpu(in); gpu.setEngine(Engine::CUDA);
            gpu.setCudaBlockSize(block);
            require(gpu.cudaLaunchConfiguration() == conway::core::planCudaLaunch(in.width,in.height,block));
            for (int i=0; i<40; ++i) { ref.step(); gpu.step(conway::core::kNoTrace); same(ref,gpu); }
            require(gpu.cudaLaunchConfiguration().block==block);
        }
        // All local neighborhoods via Game, including edges/halos.
        for (unsigned mask=0; mask<512; ++mask) {
            auto in = input;
            for (unsigned i=0;i<9;++i) in.cells[i]=(mask>>i)&1;
            Game ref(in), gpu(in); gpu.setEngine(Engine::CUDA);
            gpu.setCudaBlockSize(block);
            require(gpu.cudaLaunchConfiguration() == conway::core::planCudaLaunch(in.width,in.height,block));
            ref.step(); gpu.step(); same(ref,gpu);
        }
        } // Every preset, including all 512 neighborhoods.
        Game ref(input, std::numeric_limits<std::size_t>::max()), gpu(ref);
        gpu.setEngine(Engine::CUDA); gpu.setCudaBlockSize(conway::core::kCudaBlock32x8); ref.step(); gpu.step(); same(ref,gpu);
        Game copy(gpu), assigned(input); assigned.setEngine(Engine::CUDA); assigned.step(); assigned=gpu;
        same(copy,gpu); same(assigned,gpu);
        require(copy.cudaBlockSize() == gpu.cudaBlockSize() && assigned.cudaBlockSize() == gpu.cudaBlockSize());
        Game& mutableGame = gpu;
        mutableGame.setCell(0,0,true); ref.setCell(0,0,true);
        gpu.step(); ref.step(); same(gpu,ref);
        require(copy.generation() != gpu.generation());
        copy.step(); assigned.step(); same(copy,assigned);
        require(copy.cudaLaunchConfiguration().block==copy.cudaBlockSize() &&
                assigned.cudaLaunchConfiguration().block==assigned.cudaBlockSize());
        Game moved(std::move(gpu)); gpu=Game(input); same(moved,ref);
        require(moved.cudaBlockSize() == conway::core::kCudaBlock32x8);
        assigned=std::move(moved); assigned=std::move(assigned);
        require(assigned.cudaBlockSize() == conway::core::kCudaBlock32x8);
        assigned.step(); ref.step(); same(assigned,ref);
        assigned=assigned; same(assigned,ref);
        // Mutation through a retained Game reference must never leave stale GPU data.
        Game& retained = assigned;
        retained.clear(); ref.clear(); assigned.step(); ref.step(); same(assigned,ref);
        retained.setCell(1,1,true); ref.setCell(1,1,true);
        for (auto engine : {Engine::OpenMP, Engine::CUDA, Engine::Sequential, Engine::CUDA}) {
            assigned.setEngine(engine); assigned.step(); ref.step(); same(assigned,ref);
            require(assigned.cudaBlockSize()==conway::core::kCudaBlock32x8);
            require(assigned.cudaLaunchConfiguration().block==assigned.cudaBlockSize());
        }
        auto before = assigned;
        Sink sink;
        bool rejected=false;
        try { assigned.step(&sink); } catch (const std::logic_error&) { rejected=true; }
        require(rejected); same(assigned,before);
        assigned.step(); ref.step(); same(assigned,ref);
        // Retain reference before the first GPU allocation, then edit and change
        // geometry repeatedly while stepping directly with CUDA.
        Game changing(input), oracle(input);
        changing.setEngine(Engine::CUDA);
        Game& retainedBeforeCache = changing;
        for (int i=0;i<12;++i) {
            const auto saved = changing;
            const auto block = i%3==0 ? conway::core::kCudaBlock8x8 :
                               i%3==1 ? conway::core::kCudaBlock16x16 : conway::core::kCudaBlock32x8;
            changing.setCudaBlockSize(block); same(changing,saved);
            require(changing.cudaLaunchConfiguration() == conway::core::planCudaLaunch(3,3,block));
            bool invalid=false;
            try { changing.setCudaBlockSize({32,32}); } catch (const std::invalid_argument&) { invalid=true; }
            require(invalid && changing.cudaBlockSize()==block); same(changing,saved);
            retainedBeforeCache.setCell(i%3,(i/3)%3,true); oracle.setCell(i%3,(i/3)%3,true);
            changing.step(); oracle.step(); same(changing,oracle);
            require(changing.cudaLaunchConfiguration().block==block);
        }
        // The standalone backend changes geometry without losing either buffer.
        conway::core::CudaBackend backend(input);
        backend.step(); const auto snapshot=backend.snapshot();
        for (auto block : {conway::core::kCudaBlock8x8, conway::core::kCudaBlock16x16, conway::core::kCudaBlock32x8}) {
            backend.setBlockSize(block);
            const auto after=backend.snapshot();
            require(after.current==snapshot.current && after.next==snapshot.next &&
                    after.generation==snapshot.generation && after.aliveCount==snapshot.aliveCount);
        }
        const auto backendGeometry=backend.launchConfiguration();
        conway::core::CudaBackend movedBackend(std::move(backend)), destinationBackend(input);
        require(movedBackend.launchConfiguration()==backendGeometry);
        destinationBackend=std::move(movedBackend);
        require(destinationBackend.launchConfiguration()==backendGeometry);
        const auto movedSnapshot=destinationBackend.snapshot();
        require(movedSnapshot.current==snapshot.current && movedSnapshot.next==snapshot.next &&
                movedSnapshot.generation==snapshot.generation && movedSnapshot.aliveCount==snapshot.aliveCount);
        std::cout << "PASS all block presets, full buffers, geometry changes, copy/move ownership, edits, engine switching and trace rejection\n";
#endif
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include "core/cuda_launch.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace conway::core;
void require(bool ok) { if (!ok) throw std::runtime_error("Launch geometry contract failed"); }
int main() {
    try {
        require(kDefaultCudaBlock == kCudaBlock16x16);
        for (auto block : {kCudaBlock8x8, kCudaBlock16x16, kCudaBlock32x8}) {
            for (auto [w,h] : {std::pair{1u,1u},{3u,5u},{17u,19u},{32u,16u},{513u,517u}}) {
                const auto g = planCudaLaunch(w,h,block);
                require(g.block == block && g.threadsPerBlock == block.x * block.y);
                require(g.gridX == (w + block.x - 1)/block.x && g.gridY == (h + block.y - 1)/block.y);
                require(g.launchedPositions == g.gridX * g.gridY * g.threadsPerBlock);
                require(g.outsidePositions == g.launchedPositions - w*h);
                // Enumerate launch positions: every logical cell exactly once,
                // and every out-of-board position accounted for.
                std::size_t inside=0, outside=0;
                for (std::size_t y=0;y<g.gridY*block.y;++y)
                    for (std::size_t x=0;x<g.gridX*block.x;++x)
                        (x<w && y<h ? inside : outside)++;
                require(inside == w*h && outside == g.outsidePositions);
            }
        }
        const auto example=planCudaLaunch(37,53);
        require(example.gridX==3 && example.gridY==4 && example.launchedPositions==3072 && example.outsidePositions==1111);
        for (auto block : {CudaBlockSize{0,0}, {0,8}, {8,0}, {1,1}, {16,8}, {32,32}, {~0u,~0u}}) {
            bool rejected=false;
            try { (void)planCudaLaunch(3,5,block); } catch (const std::invalid_argument&) { rejected=true; }
            require(rejected);
        }
        for (auto [w,h] : {std::pair<std::size_t,std::size_t>{0,1},{1,0},{~std::size_t{0},2},{2,~std::size_t{0}}}) {
            bool rejected=false;
            try { (void)planCudaLaunch(w,h); } catch (const std::exception&) { rejected=true; }
            require(rejected);
        }
        bool overflow=false;
        try { (void)planCudaLaunch(std::numeric_limits<std::size_t>::max()/8,1); }
        catch (const std::overflow_error&) { overflow=true; }
        require(overflow); // Board arithmetic fits, rounded launch count does not.
        std::cout << "PASS pure 2D CUDA geometry, presets, padding and invalid input (no CUDA runtime)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

#include "app/cuda_info.hpp"
#include "app/simulation.hpp"
#include "game_test_access.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
using namespace conway;
using namespace conway::app;
void require(bool ok) {if(!ok)throw std::runtime_error("CUDA UI session contract failed");}
int main(int argc,char** argv) {
    try {
        const auto info=inspectCuda(37,53);
        if(argc>1 && std::string(argv[1])=="--availability") {
            std::cout<<info.diagnostic<<'\n';
            if(info.available)return 0;
            if(info.diagnostic.find("GPU ausente")!=std::string::npos)return 77;
            if(info.diagnostic.find("build sin CUDA")!=std::string::npos)return 2;
            return 1;
        }
        const InputData input{3,3,{0,0,0,1,1,1,0,0,0}};
        const bool unavailable=argc>1 || !info.available;
        if(unavailable) {
            if(argc==1 && info.diagnostic.find("GPU ausente")!=std::string::npos)return 77;
            require(!info.available);
            require(info.diagnostic.find("build sin CUDA")!=std::string::npos || info.diagnostic.find("GPU ausente")!=std::string::npos);
            SimulationSession session(input,Engine::Sequential);session.step();auto saved=session.game();
            bool rejected=false;
            try {session.configure(Engine::CUDA,1,core::kCudaBlock8x8);} catch(const std::exception&) {rejected=true;}
            require(rejected && session.engine()==Engine::Sequential);
            require(detail::sameGameBuffers(session.game(),saved));
            SimulationSession cuda(input,Engine::CUDA);
            require(!cuda.error().empty() && cuda.engine()==Engine::CUDA && cuda.game().generation()==0);
            cuda.step();require(cuda.game().generation()==0);
            std::cout<<info.diagnostic<<" · PASS rejection without fallback\n";return 0;
        }
        require(info.geometry && !info.gpu.empty());
        SimulationSession session(input,Engine::CUDA);Game cpu(input);
        require(!session.preview() && session.error().empty());
        for(auto block:{core::kCudaBlock8x8,core::kCudaBlock16x16,core::kCudaBlock32x8}) {
            auto saved=session.game();session.configure(Engine::CUDA,1,block);
            require(detail::sameGameBuffers(session.game(),saved));
            session.step();cpu.step();require(detail::sameGameBuffers(session.game(),cpu));
            require(session.game().cudaLaunchConfiguration().block==block);
            session.toggleRunning();session.pause(); // Menu exit/reentry freezes this same session.
            require(!session.isRunning());
            saved=session.game();
            session.configure(Engine::OpenMP,1,block);require(detail::sameGameBuffers(session.game(),saved));
            session.configure(Engine::CUDA,1,block);require(detail::sameGameBuffers(session.game(),saved));
            require(!session.preview());
        }
        session.reset();require(session.game().generation()==0 && session.game().cudaBlockSize()==core::kCudaBlock32x8);
        session.step();Game reset(input);reset.step();require(detail::sameGameBuffers(session.game(),reset));
        auto saved=session.game();bool rejected=false;
        try {session.configure(Engine::CUDA,1,{32,32});}catch(const std::invalid_argument&){rejected=true;}
        require(rejected && detail::sameGameBuffers(session.game(),saved));
        std::cout<<info.diagnostic<<" · PASS session blocks, controls, persistence, full buffers and CPU equivalence\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

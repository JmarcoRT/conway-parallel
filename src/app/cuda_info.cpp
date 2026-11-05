#include "app/cuda_info.hpp"
#ifdef CONWAY_HAS_CUDA
#include "core/engines/cuda.hpp"
#endif

namespace conway::app {
CudaUiInfo inspectCuda(std::size_t width,std::size_t height,core::CudaBlockSize block) {
    CudaUiInfo info;
#ifdef CONWAY_HAS_CUDA
    try {
        const auto status=core::cuda_device_status();
        if(status.availability==core::CudaAvailability::NoDevice) {
            info.diagnostic="CUDA no disponible: GPU ausente u oculta";return info;
        }
        if(status.availability!=core::CudaAvailability::Available) {
            info.diagnostic="CUDA: error de inicialización: "+status.diagnostic;return info;
        }
        auto geometry=core::CudaBackend::launchConfiguration(width,height,block);
        core::CudaBackend probe(InputData{1,1,{0}},0,0,block);
        info.gpu=core::CudaBackend::deviceName();
        info.geometry=geometry;info.available=true;
        info.diagnostic="CUDA disponible: "+info.gpu;
    } catch(const std::exception& e) {info.diagnostic=std::string("CUDA: error de inicialización/configuración: ")+e.what();}
#else
    (void)width;(void)height;(void)block;
    info.diagnostic="CUDA no disponible: build sin CUDA (CONWAY_ENABLE_CUDA=OFF)";
#endif
    return info;
}
std::string cudaGeometryLabel(core::CudaBlockSize block,const CudaUiInfo& info) {
    std::string text="Bloque "+std::to_string(block.x)+"x"+std::to_string(block.y)+
        " · "+std::to_string(block.x*block.y)+" hilos/bloque";
    if(info.geometry)text+=" · grid "+std::to_string(info.geometry->gridX)+"x"+std::to_string(info.geometry->gridY);
    return text;
}
}

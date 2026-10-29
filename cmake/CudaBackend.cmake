# Optional backend. OFF never probes CUDA or searches its toolkit.
option(CONWAY_ENABLE_CUDA "Build the optional resident CUDA backend" OFF)
if(CONWAY_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER)
        message(FATAL_ERROR "CONWAY_ENABLE_CUDA=ON requires a CUDA compiler/toolkit; GPU availability is checked at runtime")
    endif()
    enable_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA" OR CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 12.5)
        message(FATAL_ERROR "This backend requires NVIDIA nvcc >= 12.5")
    endif()
    find_package(CUDAToolkit 12.5 REQUIRED)
    set(CONWAY_CUDA_ARCHITECTURES "86" CACHE STRING "CUDA architectures (86: RTX 3050; 75: GTX 1650)")
    add_library(conway_cuda STATIC src/core/engines/cuda.cu)
    target_include_directories(conway_cuda PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(conway_cuda PUBLIC conway_cuda_launch PRIVATE CUDA::cudart)
    target_compile_definitions(conway_cuda PUBLIC CONWAY_HAS_CUDA=1)
    set_target_properties(conway_cuda PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON CUDA_EXTENSIONS OFF
        CUDA_ARCHITECTURES "${CONWAY_CUDA_ARCHITECTURES}"
        CUDA_RUNTIME_LIBRARY Shared)
    if(BUILD_TESTING)
        add_executable(cuda_backend_tests
            tests/cuda_backend_tests.cpp
            src/core/game.cpp src/core/engines/sequential.cpp
            src/core/engines/openmp.cpp src/core/bs23.cpp)
        target_link_libraries(cuda_backend_tests PRIVATE conway_cuda OpenMP::OpenMP_CXX)
        # GNU-compatible Linux linker wrapping is confined to this test target.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(cuda_failure_tests tests/cuda_failure_tests.cpp
                src/core/game.cpp src/core/engines/sequential.cpp
                src/core/engines/openmp.cpp src/core/bs23.cpp)
            target_link_libraries(cuda_failure_tests PRIVATE conway_cuda CUDA::cudart OpenMP::OpenMP_CXX)
            foreach(api IN ITEMS cudaMalloc cudaFree cudaStreamCreateWithFlags cudaStreamDestroy
                                 cudaMemcpyAsync cudaMemsetAsync cudaStreamSynchronize cudaGetLastError
                                 cudaGetDeviceProperties_v2 cudaFuncGetAttributes cudaGetDeviceCount cudaGetDevice)
                target_link_options(cuda_failure_tests PRIVATE "LINKER:--wrap=${api}")
            endforeach()
            add_test(NAME cuda_failure_cleanup COMMAND cuda_failure_tests)
            set_tests_properties(cuda_failure_cleanup PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30 RUN_SERIAL TRUE)
        endif()
        add_test(NAME cuda_input_validation COMMAND cuda_backend_tests --validation-only)
        add_test(NAME cuda_no_device COMMAND cuda_backend_tests --no-device)
        set_tests_properties(cuda_no_device PROPERTIES ENVIRONMENT "CUDA_VISIBLE_DEVICES=-1")
        add_test(NAME cuda_equivalence COMMAND cuda_backend_tests)
        set_tests_properties(cuda_equivalence PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
endif()

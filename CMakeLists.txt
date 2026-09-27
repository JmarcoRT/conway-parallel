cmake_minimum_required(VERSION 3.20)

project(conway_parallel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenMP REQUIRED)
find_package(MPI REQUIRED COMPONENTS CXX)

add_executable(conway
    src/main.cpp
    src/core/game.cpp
    src/input/input.cpp
)

target_include_directories(conway PRIVATE include)

target_link_libraries(conway PRIVATE
    OpenMP::OpenMP_CXX
    MPI::MPI_CXX
)

include(CTest)

if(BUILD_TESTING)
    add_executable(input_tests
        tests/input_tests.cpp
        src/input/input.cpp
        src/core/game.cpp
    )
    target_include_directories(input_tests PRIVATE include)
    add_test(NAME input_functional
        COMMAND input_tests "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

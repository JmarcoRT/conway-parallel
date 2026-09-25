cmake_minimum_required(VERSION 3.20)

project(conway_parallel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenMP REQUIRED)
find_package(MPI REQUIRED COMPONENTS CXX)

# Ejecutable principal del proyecto
add_executable(conway
    src/main.cpp
    src/core/game.cpp
    src/input/input.cpp
    src/ui/layout.cpp
    src/ui/menu.cpp
)

target_include_directories(conway PRIVATE include)

target_link_libraries(conway PRIVATE
    OpenMP::OpenMP_CXX
    MPI::MPI_CXX
)

# Target independiente de demostración para la UI (Semana 5)
add_executable(conway_ui_demo
    src/ui/demo_menu.cpp
    src/ui/layout.cpp
    src/ui/menu.cpp
)

target_include_directories(conway_ui_demo PRIVATE include)
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
    src/core/engines/sequential.cpp
    src/core/bs23.cpp
    src/input/input.cpp
    src/ui/layout.cpp
    src/ui/menu.cpp
    src/app/app.cpp
    src/app/controls.cpp
)

target_include_directories(conway PRIVATE include)

target_link_libraries(conway PRIVATE
    OpenMP::OpenMP_CXX
    MPI::MPI_CXX
)

# Prototipo MPI aislado (Semana 6): procesos + reparto horizontal de filas.
# Ejecutar con: mpirun -np P ./mpi_partition_prototype  (ver docs/MPI_PROTOTYPE.md)
add_executable(mpi_partition_prototype
    src/mpi/partition_prototype.cpp
    src/mpi/partition.cpp
    src/mpi/local_state.cpp
    src/input/input.cpp
)
target_include_directories(mpi_partition_prototype PRIVATE include)
target_link_libraries(mpi_partition_prototype PRIVATE MPI::MPI_CXX)


include(CTest)

if(BUILD_TESTING)
    add_executable(input_tests
        tests/input_tests.cpp
        src/input/input.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/bs23.cpp
    )
    target_include_directories(input_tests PRIVATE include)
    add_test(NAME input_functional
        COMMAND input_tests "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(next_cell_state_tests
        tests/test_next_cell_state.cpp
        src/core/bs23.cpp
    )
    target_include_directories(next_cell_state_tests PRIVATE include)
    add_test(NAME rule_bs23_per_cell
        COMMAND next_cell_state_tests
    )
    # Particion horizontal y GameState local: aritmetica pura, no usa MPI.
    add_executable(partition_tests
        tests/partition_tests.cpp
        src/mpi/partition.cpp
        src/mpi/local_state.cpp
    )
    target_include_directories(partition_tests PRIVATE include)
    add_test(NAME mpi_partition_unit
        COMMAND partition_tests
    )

    # Casos de correccion del motor (toad, beacon, pulsar, auto-consistencia
    # del criterio de equivalencia): aritmetica pura, no usa OpenMP/MPI.
    add_executable(conway_correctness_tests
        tests/conway_correctness_tests.cpp
        src/input/input.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/bs23.cpp
    )
    target_include_directories(conway_correctness_tests PRIVATE include)
    add_test(NAME conway_correctness
        COMMAND conway_correctness_tests "${CMAKE_CURRENT_SOURCE_DIR}"
    )

add_executable(openmp_vs_sequential
    tests/openmp_vs_sequential.cpp
    src/core/engines/openmp.cpp
    src/core/engines/sequential.cpp
    src/core/bs23.cpp
    src/input/input.cpp
)
target_include_directories(openmp_vs_sequential PRIVATE include)
target_link_libraries(openmp_vs_sequential PRIVATE OpenMP::OpenMP_CXX)
add_test(NAME openmp_vs_sequential
    COMMAND openmp_vs_sequential "${CMAKE_CURRENT_SOURCE_DIR}"
)
endif()


# Target independiente de demostración para la UI (Semana 5)
add_executable(conway_ui_demo
    src/ui/demo_menu.cpp
    src/ui/layout.cpp
    src/ui/menu.cpp
)

target_include_directories(conway_ui_demo PRIVATE include)
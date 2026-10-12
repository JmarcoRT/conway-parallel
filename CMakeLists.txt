cmake_minimum_required(VERSION 3.20)

project(conway_parallel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS_FUZZER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG cdf28903a7781f97ba94d30b79c3a4b0c97ccce7
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(ftxui)

find_package(OpenMP REQUIRED)
find_package(MPI REQUIRED COMPONENTS CXX)

# Ejecutable principal del proyecto
add_executable(conway
    src/main.cpp
    src/core/game.cpp
    src/core/engines/sequential.cpp
    src/core/engines/openmp.cpp
    src/core/bs23.cpp
    src/input/input.cpp
    src/ui/layout.cpp
    src/ui/menu.cpp
    src/app/app.cpp
    src/app/controls.cpp
    src/app/simulation.cpp
    src/app/thread_config.cpp
    src/app/pattern_catalog.cpp
    src/app/ftxui_app.cpp
    src/app/benchmark.cpp
    src/ui/board_view.cpp
    src/ui/simulation_layout.cpp
)

target_include_directories(conway PRIVATE include)

target_link_libraries(conway PRIVATE
    OpenMP::OpenMP_CXX
    MPI::MPI_CXX
    ftxui::component
    ftxui::dom
    ftxui::screen
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
        src/core/engines/openmp.cpp
        src/core/bs23.cpp
    )
    target_include_directories(input_tests PRIVATE include)
    target_link_libraries(input_tests PRIVATE OpenMP::OpenMP_CXX)
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
    # game.cpp referencia step_openmp tras S7, asi que este target tambien
    # debe enlazar openmp.cpp + OpenMP::OpenMP_CXX.
    add_executable(conway_correctness_tests
        tests/conway_correctness_tests.cpp
        src/input/input.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/engines/openmp.cpp
        src/core/bs23.cpp
    )
    target_include_directories(conway_correctness_tests PRIVATE include)
    target_link_libraries(conway_correctness_tests PRIVATE OpenMP::OpenMP_CXX)
    add_test(NAME conway_correctness
        COMMAND conway_correctness_tests "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    # Semana 7: equivalencia OpenMP vs secuencial a traves de conway::Game
    # (Engine::OpenMP) con 1, 2, 4 y 8 hilos. Misma firma que el prototipo
    # S6 (openmp_vs_sequential) pero usando la API publica de Game.
    add_executable(game_engine_tests
        tests/game_engine_tests.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/engines/openmp.cpp
        src/core/bs23.cpp
        src/input/input.cpp
    )
    target_include_directories(game_engine_tests PRIVATE include)
    target_link_libraries(game_engine_tests PRIVATE OpenMP::OpenMP_CXX)
    add_test(NAME game_openmp_vs_sequential
        COMMAND game_engine_tests "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    # Semana 7: medicion inicial exploratoria (no es el benchmark final).
    # El test de CTest solo es un "smoke test" rapido (tablero pequeno); la
    # medicion real se ejecuta a mano: ./build/openmp_timing o
    # scripts/measure_openmp.sh (ver docs/OPENMP_INTEGRATION.md).
    add_executable(openmp_timing
        tests/openmp_timing.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/engines/openmp.cpp
        src/core/bs23.cpp
        src/input/input.cpp
    )
    target_include_directories(openmp_timing PRIVATE include)
    target_link_libraries(openmp_timing PRIVATE OpenMP::OpenMP_CXX)
    add_test(NAME openmp_timing_smoke
        COMMAND openmp_timing --width 64 --height 64 --generations 5 --repeats 1
    )

    # Semana 7 (continuacion): smoke test del submenu de seleccion de
    # motor desde la aplicacion. Verifica navegacion, cambio a OpenMP y
    # persistencia entre cargas de patron.
    add_test(NAME engine_selection_smoke
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_engine_selection.sh"
                "$<TARGET_FILE:conway>"
    )

    add_executable(app_runtime_tests
        tests/app_runtime_tests.cpp
        src/app/simulation.cpp
        src/app/thread_config.cpp
        src/app/pattern_catalog.cpp
        src/app/benchmark.cpp
        src/ui/layout.cpp
        src/ui/simulation_layout.cpp
        src/ui/board_view.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/engines/openmp.cpp
        src/core/bs23.cpp
        src/input/input.cpp
    )
    target_include_directories(app_runtime_tests PRIVATE include)
    target_link_libraries(app_runtime_tests PRIVATE OpenMP::OpenMP_CXX)
    add_test(NAME app_runtime
        COMMAND app_runtime_tests
    )
    set_tests_properties(app_runtime PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=8"
    )
    add_test(NAME app_runtime_thread_limit
        COMMAND app_runtime_tests --thread-limit-one
    )
    set_tests_properties(app_runtime_thread_limit PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=1"
    )

    add_test(NAME app_functional_smoke
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_functional.sh"
                "$<TARGET_FILE:conway>"
    )
    set_tests_properties(app_functional_smoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
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

# Vista del tablero (current / next / generacion / leyenda). Semana 5.
add_executable(conway_view_demo
    src/ui/demo_view.cpp
    src/ui/board_view.cpp
    src/core/game.cpp
    src/core/engines/sequential.cpp
    src/core/engines/openmp.cpp
    src/core/bs23.cpp
    src/input/input.cpp
)
target_include_directories(conway_view_demo PRIVATE include)
target_link_libraries(conway_view_demo PRIVATE OpenMP::OpenMP_CXX)

# Layout de simulación (3 paneles: current, next, ejecucion). Semana 6.
# Es un demo con datos mock — no usa conway::Game, por lo que no necesita
# enlazar src/core/ ni los motores. El target es standalone.
add_executable(conway_simulation_demo
    src/ui/demo_simulation.cpp
    src/ui/simulation_layout.cpp
    src/ui/layout.cpp
)
target_include_directories(conway_simulation_demo PRIVATE include)

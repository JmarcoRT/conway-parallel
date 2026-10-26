cmake_minimum_required(VERSION 3.20)

project(conway_parallel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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


# Standalone reproducible input module: standard C++ only, no engine or UI.
add_library(conway_generated_input STATIC src/input/generated_input.cpp)
target_include_directories(conway_generated_input PUBLIC include)

# Experimental runner is independent of FTXUI; MPI executes in an external worker.
add_library(conway_experiments STATIC
    src/app/scaling_study.cpp
    src/app/experimental_study.cpp
    src/app/experiment_runner.cpp
    src/app/benchmark.cpp
    src/core/game.cpp
    src/core/engines/sequential.cpp
    src/core/engines/openmp.cpp
    src/core/bs23.cpp
)
target_include_directories(conway_experiments PUBLIC include)
target_link_libraries(conway_experiments PUBLIC OpenMP::OpenMP_CXX conway_mpi_client)
target_compile_definitions(conway_experiments PRIVATE CONWAY_EXPERIMENT_MPI_LAUNCHER="${MPIEXEC_EXECUTABLE}")

# Snapshot generated compile commands and Git only in a controlled build step.
set(experiment_snapshot_multi_config FALSE)
if(CMAKE_CONFIGURATION_TYPES)
    set(experiment_snapshot_multi_config TRUE)
endif()
add_custom_target(experiment_build_snapshot
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DCOMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}"
        "-DCOMPILER_PATH=${CMAKE_CXX_COMPILER}"
        "-DCONFIG=$<CONFIG>"
        "-DMULTI_CONFIG=${experiment_snapshot_multi_config}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CaptureExperimentBuild.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/experiment_build_info.hpp"
    VERBATIM)
add_library(conway_experiment_export STATIC
    src/app/experiment_metadata.cpp
    src/app/experiment_csv.cpp)
add_dependencies(conway_experiment_export experiment_build_snapshot)
target_include_directories(conway_experiment_export PUBLIC include
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(conway_experiment_export PRIVATE CONWAY_CAPTURED_BUILD_INFO=1)
target_link_libraries(conway_experiment_export PUBLIC conway_experiments)

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
    src/app/thread_config.cpp
    src/app/pattern_catalog.cpp
    src/app/pattern_preview.cpp
    src/app/didactic_session.cpp
    src/app/ftxui_app.cpp
    src/app/experiment_execution.cpp
    src/app/comparison_view.cpp
    src/app/experimental_study_view.cpp
    src/app/scaling_view.cpp
    src/app/experiment_form.cpp
    src/app/experiment_form_view.cpp
    src/app/benchmark.cpp
    src/ui/board_view.cpp
    src/ui/simulation_layout.cpp
)

target_include_directories(conway PRIVATE include)

target_link_libraries(conway PRIVATE
    conway_experiment_export conway_generated_input
    OpenMP::OpenMP_CXX
    MPI::MPI_CXX
    ftxui::component
    ftxui::dom
    ftxui::screen
)

# Prototipo MPI aislado (Semana 6): procesos + reparto horizontal de filas.
# Ejecutar con: mpirun -np P ./mpi_partition_prototype  (ver docs/MPI_PROTOTYPE.md)
# Portable protocol/capture contract shared by client and worker (no MPI runtime).
add_library(conway_mpi_protocol STATIC src/mpi/protocol.cpp src/mpi/step_trace.cpp src/mpi/partition.cpp)
target_include_directories(conway_mpi_protocol PUBLIC include)
add_library(conway_mpi STATIC
    src/core/engines/mpi.cpp
    src/core/engines/sequential.cpp
    src/core/bs23.cpp
    src/mpi/local_state.cpp)
target_include_directories(conway_mpi PUBLIC include)
target_link_libraries(conway_mpi PUBLIC MPI::MPI_CXX conway_mpi_protocol)

add_library(conway_mpi_client STATIC src/mpi/client.cpp)
target_link_libraries(conway_mpi_client PUBLIC conway_mpi_protocol)
target_include_directories(conway_mpi_client PUBLIC include)
add_library(conway_simulation STATIC src/app/simulation.cpp)
target_include_directories(conway_simulation PUBLIC include)
target_link_libraries(conway_simulation PUBLIC conway_mpi_client OpenMP::OpenMP_CXX)
target_compile_definitions(conway_simulation PRIVATE CONWAY_MPI_LAUNCHER="${MPIEXEC_EXECUTABLE}")
target_link_libraries(conway PRIVATE conway_simulation)

add_executable(conway_mpi_worker src/mpi/worker.cpp)
target_link_libraries(conway_mpi_worker PRIVATE conway_mpi)

add_executable(mpi_partition_prototype
    src/mpi/partition_prototype.cpp
    src/input/input.cpp
)
target_include_directories(mpi_partition_prototype PRIVATE include)
target_link_libraries(mpi_partition_prototype PRIVATE conway_mpi)


include(CTest)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(experimental_review_tests tests/experimental_review_tests.cpp src/app/experiment_execution.cpp)
    target_link_libraries(experimental_review_tests PRIVATE conway_experiment_export conway_generated_input)
    add_test(NAME experimental_review COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_experimental_review_csv.py" $<TARGET_FILE:experimental_review_tests>)
    set_tests_properties(experimental_review PROPERTIES TIMEOUT 15)

    add_test(NAME experimental_visualization_guard_pty COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_experimental_visualization_guard.py"
        $<TARGET_FILE:conway> "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(experimental_visualization_guard_pty PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE ENVIRONMENT "OMP_NUM_THREADS=2")

    add_executable(experimental_mpi_tests tests/experimental_mpi_tests.cpp src/app/experiment_execution.cpp)
    target_link_libraries(experimental_mpi_tests PRIVATE conway_experiment_export conway_generated_input)
    add_dependencies(experimental_mpi_tests conway_mpi_worker)
    add_test(NAME experimental_mpi COMMAND experimental_mpi_tests "${MPIEXEC_EXECUTABLE}" $<TARGET_FILE:conway_mpi_worker>
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mpi_exclusive_launcher.py")
    set_tests_properties(experimental_mpi PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE PROCESSORS 4
        ENVIRONMENT "OMP_NUM_THREADS=2;OMP_DYNAMIC=FALSE")
    add_executable(mpi_batch_metrics_tests tests/mpi_batch_metrics_tests.cpp)
    target_link_libraries(mpi_batch_metrics_tests PRIVATE conway_mpi)
    add_test(NAME mpi_batch_metrics COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
        ${MPIEXEC_PREFLAGS} --oversubscribe $<TARGET_FILE:mpi_batch_metrics_tests> ${MPIEXEC_POSTFLAGS})
    set_tests_properties(mpi_batch_metrics PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE PROCESSORS 4)

    add_executable(mpi_simulation_tests tests/mpi_simulation_tests.cpp
        src/core/game.cpp src/core/engines/sequential.cpp src/core/engines/openmp.cpp src/core/bs23.cpp)
    target_link_libraries(mpi_simulation_tests PRIVATE conway_simulation)
    add_dependencies(mpi_simulation_tests conway_mpi_worker)
    add_test(NAME mpi_simulation COMMAND mpi_simulation_tests "${MPIEXEC_EXECUTABLE}" $<TARGET_FILE:conway_mpi_worker>)
    set_tests_properties(mpi_simulation PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE PROCESSORS 4
        ENVIRONMENT "OMP_NUM_THREADS=2")
    add_executable(mpi_trace_failure_worker src/mpi/worker.cpp)
    target_link_libraries(mpi_trace_failure_worker PRIVATE conway_mpi)
    target_compile_definitions(mpi_trace_failure_worker PRIVATE CONWAY_MPI_TRACE_FAIL_AFTER_STEP=1)
    add_executable(mpi_trace_tests tests/mpi_trace_tests.cpp)
    target_link_libraries(mpi_trace_tests PRIVATE conway_mpi_client conway_mpi)
    add_dependencies(mpi_trace_tests conway_mpi_worker mpi_trace_failure_worker)
    add_test(NAME mpi_trace_client COMMAND mpi_trace_tests "${MPIEXEC_EXECUTABLE}" $<TARGET_FILE:conway_mpi_worker> $<TARGET_FILE:mpi_trace_failure_worker>)
    set_tests_properties(mpi_trace_client PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE PROCESSORS 7)
    foreach(processes IN ITEMS 1 2 4 7)
        add_test(NAME mpi_trace_engine_${processes} COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${processes}
            ${MPIEXEC_PREFLAGS} --oversubscribe $<TARGET_FILE:mpi_trace_tests> --engine ${MPIEXEC_POSTFLAGS})
        set_tests_properties(mpi_trace_engine_${processes} PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE PROCESSORS ${processes})
    endforeach()
    add_executable(mpi_client_tests tests/mpi_client_tests.cpp src/input/input.cpp)
    target_link_libraries(mpi_client_tests PRIVATE conway_mpi_client conway_mpi)
    add_dependencies(mpi_client_tests conway_mpi_worker)
    add_test(NAME mpi_client COMMAND mpi_client_tests "${MPIEXEC_EXECUTABLE}"
        $<TARGET_FILE:conway_mpi_worker> "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(mpi_client PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE PROCESSORS 4)
    add_executable(mpi_evolution_tests tests/mpi_evolution_tests.cpp src/input/input.cpp)
    target_link_libraries(mpi_evolution_tests PRIVATE conway_mpi)
    foreach(processes IN ITEMS 1 2 3 4 7 8)
        add_test(NAME mpi_evolution_${processes}
            COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${processes}
                ${MPIEXEC_PREFLAGS} --oversubscribe $<TARGET_FILE:mpi_evolution_tests>
                ${MPIEXEC_POSTFLAGS} "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(mpi_evolution_${processes} PROPERTIES TIMEOUT 45
            PROCESSORS ${processes} RUN_SERIAL TRUE)
    endforeach()
    foreach(processes IN ITEMS 1 8)
        add_test(NAME mpi_prototype_evolution_${processes}
            COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${processes}
                ${MPIEXEC_PREFLAGS} --oversubscribe $<TARGET_FILE:mpi_partition_prototype>
                ${MPIEXEC_POSTFLAGS} --synthetic 3 5 --steps 30)
        set_tests_properties(mpi_prototype_evolution_${processes} PROPERTIES
            TIMEOUT 45 PROCESSORS ${processes} RUN_SERIAL TRUE)
    endforeach()
    add_executable(experimental_study_view_tests tests/experimental_study_view_tests.cpp
        src/app/experimental_study_view.cpp src/app/comparison_view.cpp src/app/scaling_view.cpp)
    target_include_directories(experimental_study_view_tests PRIVATE include)
    target_link_libraries(experimental_study_view_tests PRIVATE conway_experiments ftxui::dom ftxui::screen)
    add_test(NAME experimental_study_view COMMAND experimental_study_view_tests)

    add_executable(scaling_view_tests tests/scaling_view_tests.cpp src/app/scaling_view.cpp src/app/experimental_study_view.cpp)
    target_include_directories(scaling_view_tests PRIVATE include)
    target_link_libraries(scaling_view_tests PRIVATE conway_experiments ftxui::dom ftxui::screen)
    add_test(NAME scaling_view COMMAND scaling_view_tests)
    add_executable(comparison_view_tests tests/comparison_view_tests.cpp src/app/comparison_view.cpp src/app/experimental_study_view.cpp)
    target_include_directories(comparison_view_tests PRIVATE include)
    target_link_libraries(comparison_view_tests PRIVATE conway_experiments ftxui::dom ftxui::screen)
    add_test(NAME comparison_view COMMAND comparison_view_tests)
    add_executable(experiment_execution_tests tests/experiment_execution_tests.cpp src/app/experiment_execution.cpp src/app/experiment_form.cpp)
    target_include_directories(experiment_execution_tests PRIVATE include)
    target_link_libraries(experiment_execution_tests PRIVATE conway_experiment_export conway_generated_input)
    add_test(NAME experiment_execution COMMAND experiment_execution_tests)
    set_tests_properties(experiment_execution PROPERTIES ENVIRONMENT "OMP_THREAD_LIMIT=2" TIMEOUT 30)
    add_executable(experiment_form_tests tests/experiment_form_tests.cpp src/app/experiment_form.cpp)
    target_include_directories(experiment_form_tests PRIVATE include)
    add_test(NAME experiment_form COMMAND experiment_form_tests)

    add_executable(experiment_csv_tests tests/experiment_csv_tests.cpp src/input/input.cpp)
    target_link_libraries(experiment_csv_tests PRIVATE conway_experiment_export conway_generated_input)
    add_test(NAME experiment_csv COMMAND experiment_csv_tests "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(experiment_csv PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=8")

    add_executable(scaling_study_tests tests/scaling_study_tests.cpp src/input/input.cpp)
    target_link_libraries(scaling_study_tests PRIVATE conway_experiments conway_generated_input)
    add_test(NAME scaling_study COMMAND scaling_study_tests "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(scaling_study PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=8")
    add_test(NAME scaling_study_thread_limit
        COMMAND scaling_study_tests "${CMAKE_CURRENT_SOURCE_DIR}" --thread-limit-one)
    set_tests_properties(scaling_study_thread_limit PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=1")

    add_executable(experiment_runner_tests tests/experiment_runner_tests.cpp src/input/input.cpp)
    target_link_libraries(experiment_runner_tests PRIVATE conway_experiments conway_generated_input)
    add_test(NAME experiment_runner COMMAND experiment_runner_tests "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(experiment_runner PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=8")
    add_test(NAME experiment_runner_thread_limit
        COMMAND experiment_runner_tests "${CMAKE_CURRENT_SOURCE_DIR}" --thread-limit-one)
    set_tests_properties(experiment_runner_thread_limit PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=1")

    add_executable(benchmark_statistics_tests
        tests/benchmark_statistics_tests.cpp
        src/app/benchmark.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/engines/openmp.cpp
        src/core/bs23.cpp
    )
    target_include_directories(benchmark_statistics_tests PRIVATE include)
    target_link_libraries(benchmark_statistics_tests PRIVATE OpenMP::OpenMP_CXX)
    add_test(NAME benchmark_statistics COMMAND benchmark_statistics_tests)

    add_executable(generated_input_tests
        tests/generated_input_tests.cpp
        src/core/game.cpp
        src/core/engines/sequential.cpp
        src/core/engines/openmp.cpp
        src/core/bs23.cpp
    )
    target_link_libraries(generated_input_tests PRIVATE conway_generated_input OpenMP::OpenMP_CXX)
    add_test(NAME generated_input COMMAND generated_input_tests)
    set_tests_properties(generated_input PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=8")

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

    add_test(NAME input_dimension_regressions COMMAND input_tests "${CMAKE_CURRENT_SOURCE_DIR}" --dimension-regressions)

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
        src/app/thread_config.cpp
        src/app/pattern_catalog.cpp
        src/app/pattern_preview.cpp
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
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME app_runtime_thread_limit
        COMMAND app_runtime_tests --thread-limit-one
    )
    set_tests_properties(app_runtime_thread_limit PROPERTIES
        ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=1"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
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

# Tests de la instrumentación opcional de motores (StepTrace / WorkerRecord
# / StepTraceSink). Cubre: equivalencia con/sin sink, cobertura sin
# solapes, sinks concurrentes, no residual, height == 0, hilos ociosos.
add_executable(step_trace_tests
    tests/step_trace_tests.cpp
    src/core/game.cpp
    src/core/engines/sequential.cpp
    src/core/engines/openmp.cpp
    src/core/bs23.cpp
    src/input/input.cpp
)
target_include_directories(step_trace_tests PRIVATE include)
target_link_libraries(step_trace_tests PRIVATE OpenMP::OpenMP_CXX)
add_test(NAME step_trace
    COMMAND step_trace_tests "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Tests del controlador de visualizacion didactica. Cubre equivalencia
# con el motor, reproduccion, play/pause, velocidad, cobertura y
# navegacion entre generaciones. No usa FTXUI.
add_executable(didactic_session_tests
    tests/didactic_session_tests.cpp
    src/app/didactic_session.cpp
    src/core/game.cpp
    src/core/engines/sequential.cpp
    src/core/engines/openmp.cpp
    src/core/bs23.cpp
    src/input/input.cpp
)
target_include_directories(didactic_session_tests PRIVATE include)
target_link_libraries(didactic_session_tests PRIVATE OpenMP::OpenMP_CXX)
add_test(NAME didactic_session
    COMMAND didactic_session_tests "${CMAKE_CURRENT_SOURCE_DIR}"
)
# Compila los helpers reales de FTXUI para comprobar coordenadas renderizadas.
get_target_property(didactic_layout_sources conway SOURCES)
list(REMOVE_ITEM didactic_layout_sources src/main.cpp src/app/ftxui_app.cpp)
add_executable(didactic_layout_tests tests/didactic_layout_tests.cpp ${didactic_layout_sources})
target_include_directories(didactic_layout_tests PRIVATE include)
target_link_libraries(didactic_layout_tests PRIVATE conway_experiment_export conway_generated_input OpenMP::OpenMP_CXX MPI::MPI_CXX
    ftxui::component ftxui::dom ftxui::screen)
add_test(NAME didactic_layout COMMAND didactic_layout_tests)
add_executable(ftxui_execution_fixture tests/ftxui_execution_fixture.cpp ${didactic_layout_sources})
target_include_directories(ftxui_execution_fixture PRIVATE include)
target_link_libraries(ftxui_execution_fixture PRIVATE conway_experiment_export conway_generated_input
    OpenMP::OpenMP_CXX MPI::MPI_CXX ftxui::component ftxui::dom ftxui::screen)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ftxui_mpi_pty COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_mpi.py" $<TARGET_FILE:conway> "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(ftxui_mpi_pty PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "OMP_NUM_THREADS=2")
add_test(NAME text_mpi COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_text_mpi.py" $<TARGET_FILE:conway> "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(text_mpi PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)

add_test(NAME experiment_csv_roundtrip
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_experiment_csv.py"
            "$<TARGET_FILE:experiment_csv_tests>" "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(experiment_csv_roundtrip PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_THREAD_LIMIT=8")
add_executable(responsive_layout_tests tests/responsive_layout_tests.cpp)
target_include_directories(responsive_layout_tests PRIVATE include)
target_compile_options(responsive_layout_tests PRIVATE -UNDEBUG)
add_test(NAME responsive_layout COMMAND responsive_layout_tests)
add_test(NAME ftxui_accessibility_pty
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_accessibility.py"
            "$<TARGET_FILE:conway>")
set_tests_properties(ftxui_accessibility_pty PROPERTIES TIMEOUT 150)
add_test(NAME ftxui_responsive_pty
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_responsive.py"
            "$<TARGET_FILE:conway>")
set_tests_properties(ftxui_responsive_pty PROPERTIES TIMEOUT 150)
add_test(NAME ftxui_header_pty
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_header.py"
            "$<TARGET_FILE:conway>")
set_tests_properties(ftxui_header_pty PROPERTIES TIMEOUT 150)
add_test(NAME ftxui_menu_pty
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_menu.py"
            "$<TARGET_FILE:conway>")
set_tests_properties(ftxui_menu_pty PROPERTIES TIMEOUT 60)
add_test(NAME ftxui_result_scroll_pty COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_result_scroll.py" $<TARGET_FILE:conway> "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(ftxui_result_scroll_pty PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME ftxui_experimental_mpi_pty COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_experimental_mpi.py" $<TARGET_FILE:conway> "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(ftxui_experimental_mpi_pty PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
add_test(NAME ftxui_benchmark_pty
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ftxui_benchmark.py"
            "$<TARGET_FILE:conway>" "$<TARGET_FILE:ftxui_execution_fixture>")
set_tests_properties(ftxui_benchmark_pty PROPERTIES TIMEOUT 150)

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

# All applications/fixtures use the same simulation session and MPI runtime path.
foreach(session_target IN ITEMS app_runtime_tests didactic_layout_tests ftxui_execution_fixture)
    if(TARGET ${session_target})
        target_link_libraries(${session_target} PRIVATE conway_simulation)
    endif()
endforeach()
add_dependencies(conway conway_mpi_worker)

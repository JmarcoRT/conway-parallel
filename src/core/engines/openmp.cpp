#include "core/engines/openmp.hpp"

#include "core/bs23.hpp"
#include "core/game_state.hpp"
#include "core/step_trace.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <omp.h>
#include <vector>

namespace conway::core {

namespace {

thread_local int g_lastOpenMPTeamSize = 1;

}  // namespace

int last_openmp_team_size() noexcept {
    return g_lastOpenMPTeamSize;
}

void step_openmp(GameState& s, StepTraceSink* sink) {
    // Punteros y dimensiones en variables locales const: todos los hilos
    // las comparten en modo solo lectura (shared).
    const std::uint8_t* const pIn  = s.current.data();
    std::uint8_t* const       pOut = s.next.data();
    const std::size_t stride = s.stride;
    const std::size_t width  = s.width;
    const std::ptrdiff_t height = static_cast<std::ptrdiff_t>(s.height);

    const bool tracing = (sink != kNoTrace);

    std::size_t nextAlive = 0;

    // Vector preasignado FUERA de la región paralela. Solo se asigna
    // memoria cuando la traza está activa. Cada hilo escribe exclusivamente
    // a workers[omp_get_thread_num()], por construcción del team paralelo:
    // ningún par de hilos comparte slot, no hace falta sincronización.
    // El tamaño se toma de omp_get_max_threads() para cubrir el caso peor
    // sin reasignar dentro del bucle.
    std::vector<WorkerRecord> workers;
    if (tracing) {
        workers.resize(static_cast<std::size_t>(omp_get_max_threads()));
    }

    using Clock = std::chrono::steady_clock;
    const std::size_t gen_before = s.generation;
    const auto t_step_begin = tracing ? Clock::now() : Clock::time_point{};

    // Reparto: las filas logicas 1..height se dividen en bloques contiguos
    // (schedule(static)). Cada hilo recorre sus filas completas, asi que
    // escribe en una region de `next` que ningun otro hilo toca: no hay
    // condiciones de carrera y no hacen falta secciones criticas.
    // `r`, `row` y `c` son privadas por estar declaradas dentro del bucle.
    // aliveCount se acumula con reduction(+): cada hilo lleva una copia
    // privada y OpenMP las suma al cerrar la region paralela.
    //
    // Instrumentación (sólo si tracing): cada hilo observa en el bucle el
    // primer y último r que ve, mide la duración del bucle con
    // std::chrono::steady_clock y deposita el resultado en
    // workers[omp_get_thread_num()]. La observación del rango es genuina:
    // depende de que schedule(static) entregue iteraciones contiguas, lo
    // cual es el caso para este motor. La representación no se generaliza
    // a planificadores que pudieran fragmentar el chunk.
#pragma omp parallel reduction(+ : nextAlive)
    {
        const int tid = omp_get_thread_num();
        const int nthreads = omp_get_num_threads();

        const std::size_t kUnset = static_cast<std::size_t>(-1);
        std::size_t first_row_observed = kUnset;
        std::size_t last_row_observed  = 0;
        const auto t_loop_begin = tracing ? Clock::now() : Clock::time_point{};

#pragma omp master
        g_lastOpenMPTeamSize = nthreads;

// La barrera implicita de este for queda DENTRO de loop_duration.
#pragma omp for schedule(static)
        for (std::ptrdiff_t r = 1; r <= height; ++r) {
            const std::size_t row = static_cast<std::size_t>(r);

            // Observación del rango: la primera y la última iteración
            // que ve este hilo dentro de su chunk. Con schedule(static)
            // estas dos marcan un rango contiguo de filas.
            if (tracing) {
                if (first_row_observed == kUnset) {
                    first_row_observed = row;
                }
                last_row_observed = row;
            }

            for (std::size_t c = 1; c <= width; ++c) {
                const std::uint8_t cell = next_cell_state(pIn, stride, row, c);
                pOut[row * stride + c] = cell;
                nextAlive += cell;
            }
        }

        if (tracing) {
            // El reloj final se toma despues del for y su espera de barrera;
            // no representa exclusivamente computo activo del trabajador.
            // workers[tid] es único: omp_get_thread_num() es único en el
            // team paralelo. No hay race con otros hilos.
            WorkerRecord& rec = workers[static_cast<std::size_t>(tid)];
            rec.thread_id = tid;
            rec.first_row = first_row_observed;
            rec.end_row   = last_row_observed + 1;  // exclusivo
            rec.cells_processed = (rec.end_row - rec.first_row) * width;
            rec.loop_duration   = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - t_loop_begin).count());
        }
    }
    // Barrera implícita: todos los workers han escrito a workers[tid] antes
    // de salir del parallel. La barrera libera cuando el último hilo
    // termina la región.

    s.aliveCount = nextAlive;
    swap_buffers(s);
    ++s.generation;

    if (tracing) {
        const std::uint64_t step_duration = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - t_step_begin).count());

        StepTrace trace;
        trace.engine_name      = "openmp";
        trace.requested_threads = 0;  // desconocido para el motor
        trace.effective_threads = g_lastOpenMPTeamSize;
        trace.width             = width;
        trace.height            = static_cast<std::size_t>(height);
        trace.generation_before = gen_before;
        trace.generation_after  = s.generation;
        trace.step_duration     = step_duration;

        // Filtramos los workers que no recibieron iteración (active=false
        // implícito: first_row == kUnset => end_row == 0+1 == 1, no
        // añaden cobertura). Conservamos el orden por thread_id.
        trace.workers.reserve(workers.size());
        for (auto& w : workers) {
            if (w.first_row != static_cast<std::size_t>(-1)) {
                trace.workers.push_back(w);
            }
        }

        // Fuera de la región paralela: solo el hilo original ejecuta.
        sink->onStepComplete(std::move(trace));
    }
}

}  // namespace conway::core

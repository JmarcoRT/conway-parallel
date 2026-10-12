#include "core/engines/sequential.hpp"

#include "core/bs23.hpp"
#include "core/game_state.hpp"
#include "core/step_trace.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace conway::core {

void step_sequential(GameState& s, StepTraceSink* sink) {
    const std::uint8_t* const pIn  = s.current.data();
    std::uint8_t* const       pOut = s.next.data();

    const std::size_t height = s.height;
    const std::size_t width  = s.width;
    const std::size_t stride = s.stride;
    const bool tracing = (sink != kNoTrace);

    using Clock = std::chrono::steady_clock;
    const std::size_t gen_before = s.generation;
    const auto t_step_begin = tracing ? Clock::now() : Clock::time_point{};

    std::size_t nextAlive = 0;

    // Observación del primer y último r. Sólo se actualizan si tracing.
    // Para height == 0, el bucle no se ejecuta y first_row queda en su
    // valor centinela; end_row también. La traza resultante tendrá
    // workers vacío.
    const std::size_t kUnset = static_cast<std::size_t>(-1);
    std::size_t first_row_observed = kUnset;
    std::size_t last_row_observed  = 0;
    const auto t_loop_begin = tracing ? Clock::now() : Clock::time_point{};

    for (std::size_t r = 1; r <= height; ++r) {
        if (tracing) {
            if (first_row_observed == kUnset) {
                first_row_observed = r;
            }
            last_row_observed = r;
        }
        for (std::size_t c = 1; c <= width; ++c) {
            const std::uint8_t cell =
                next_cell_state(pIn, stride, r, c);
            pOut[r * stride + c] = cell;
            nextAlive += cell;
        }
    }

    // Captura separada para la duración del bucle (no incluye la
    // sección posterior: aliveCount, swap_buffers, etc.).
    const auto t_loop_end = tracing ? Clock::now() : Clock::time_point{};
    const auto t_step_end  = t_loop_end;

    s.aliveCount = nextAlive;
    swap_buffers(s);
    ++s.generation;

    if (tracing) {
        const std::uint64_t step_duration =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t_step_end - t_step_begin).count());

        StepTrace trace;
        trace.engine_name      = "sequential";
        trace.requested_threads = 0;  // desconocido: el motor no lo recibe
        trace.effective_threads = 1;
        trace.width             = width;
        trace.height            = height;
        trace.generation_before = gen_before;
        trace.generation_after  = s.generation;
        trace.step_duration     = step_duration;

        if (height > 0 && first_row_observed != kUnset) {
            WorkerRecord rec;
            rec.thread_id       = 0;
            rec.first_row       = first_row_observed;
            rec.end_row         = last_row_observed + 1;  // exclusivo
            rec.cells_processed = (rec.end_row - rec.first_row) * width;
            rec.loop_duration   = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t_loop_end - t_loop_begin).count());
            trace.workers.push_back(rec);
        }
        // height == 0: no se añade ningún worker; verifyCoverage debe
        // tolerarlo (la suma es 0 y coincide con height).

        sink->onStepComplete(std::move(trace));
    }
}

}  // namespace conway::core

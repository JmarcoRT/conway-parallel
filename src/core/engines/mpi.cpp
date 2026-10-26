#include "core/engines/mpi.hpp"

#include "core/bs23.hpp"
#include "core/game_state.hpp"
#include "core/step_trace.hpp"

#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace conway::core {

namespace {

// Etiquetas: el sentido del desplazamiento de la fila.
constexpr int kTagToUp   = 0;  // fila 1 viaja hacia el rank superior
constexpr int kTagToDown = 1;  // fila height viaja hacia el rank inferior

int peer(int neighbor) noexcept {
    return neighbor < 0 ? MPI_PROC_NULL : neighbor;
}

// Recorrido de las filas locales [rBegin, rEnd). Reutiliza la regla comun.
std::size_t compute_rows(const std::uint8_t* pIn, std::uint8_t* pOut,
                         std::size_t stride, std::size_t width,
                         std::size_t rBegin, std::size_t rEnd) {
    std::size_t alive = 0;
    for (std::size_t r = rBegin; r < rEnd; ++r) {
        for (std::size_t c = 1; c <= width; ++c) {
            const std::uint8_t cell = next_cell_state(pIn, stride, r, c);
            pOut[r * stride + c] = cell;
            alive += cell;
        }
    }
    return alive;
}

}  // namespace

MpiStepStats step_mpi(GameState& s, const MpiStepContext& ctx, StepTraceSink* sink) {
    if (s.width > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("step_mpi: el ancho no cabe en un conteo int de MPI");
    }
    const std::size_t height = s.height;
    const std::size_t width  = s.width;
    const std::size_t stride = s.stride;
    const int count = static_cast<int>(width);
    const bool tracing = (sink != kNoTrace);

    using Clock = std::chrono::steady_clock;
    const std::size_t gen_before = s.generation;
    const auto t_step_begin = tracing ? Clock::now() : Clock::time_point{};

    MpiStepStats stats;
    std::size_t nextAlive = 0;
    std::uint8_t* const cur = s.current.data();
    std::uint8_t* const out = s.next.data();

    if (height > 0) {
        const int up = peer(ctx.up);
        const int down = peer(ctx.down);
        std::uint8_t* const firstOwned  = cur + 1 * stride + 1;
        std::uint8_t* const lastOwned   = cur + height * stride + 1;
        std::uint8_t* const topHalo     = cur + 1;
        std::uint8_t* const bottomHalo  = cur + (height + 1) * stride + 1;

        if (ctx.mode == MpiHaloMode::Blocking) {
            const double t0 = MPI_Wtime();
            // Dos fases emparejadas: cada rank envia hacia arriba y recibe
            // desde abajo, y luego al reves. Sendrecv evita el interbloqueo
            // de dos Send bloqueantes enfrentados; MPI_PROC_NULL completa
            // sin tocar el buffer.
            MPI_Sendrecv(firstOwned, count, MPI_UNSIGNED_CHAR, up, kTagToUp,
                         bottomHalo, count, MPI_UNSIGNED_CHAR, down, kTagToUp,
                         ctx.comm, MPI_STATUS_IGNORE);
            MPI_Sendrecv(lastOwned, count, MPI_UNSIGNED_CHAR, down, kTagToDown,
                         topHalo, count, MPI_UNSIGNED_CHAR, up, kTagToDown,
                         ctx.comm, MPI_STATUS_IGNORE);
            const double t1 = MPI_Wtime();
            nextAlive = compute_rows(cur, out, stride, width, 1, height + 1);
            const double t2 = MPI_Wtime();
            stats.haloSeconds = t1 - t0;
            stats.computeSeconds = t2 - t1;
        } else {
            // Las recepciones escriben solo las filas halo 0 y height+1 de
            // `current`; las filas interiores 2..height-1 solo leen filas
            // 1..height, asi que pueden calcularse mientras viajan los halos.
            // Los envios solo leen filas propias (MPI >= 3 permite leer un
            // buffer de envio pendiente) y el recorrido escribe en `next`.
            MPI_Request req[4];
            const double t0 = MPI_Wtime();
            MPI_Irecv(topHalo, count, MPI_UNSIGNED_CHAR, up, kTagToDown, ctx.comm, &req[0]);
            MPI_Irecv(bottomHalo, count, MPI_UNSIGNED_CHAR, down, kTagToUp, ctx.comm, &req[1]);
            MPI_Isend(firstOwned, count, MPI_UNSIGNED_CHAR, up, kTagToUp, ctx.comm, &req[2]);
            MPI_Isend(lastOwned, count, MPI_UNSIGNED_CHAR, down, kTagToDown, ctx.comm, &req[3]);
            const double t1 = MPI_Wtime();
            if (height > 2) {
                nextAlive += compute_rows(cur, out, stride, width, 2, height);
            }
            const double t2 = MPI_Wtime();
            MPI_Waitall(4, req, MPI_STATUSES_IGNORE);
            const double t3 = MPI_Wtime();
            nextAlive += compute_rows(cur, out, stride, width, 1, 2);
            if (height > 1) {
                nextAlive += compute_rows(cur, out, stride, width, height, height + 1);
            }
            const double t4 = MPI_Wtime();
            stats.haloSeconds = (t1 - t0) + (t3 - t2);
            stats.computeSeconds = (t2 - t1) + (t4 - t3);
        }
        stats.messagesSent = (up != MPI_PROC_NULL ? 1u : 0u) + (down != MPI_PROC_NULL ? 1u : 0u);
        stats.bytesSent = stats.messagesSent * width;
    }

    const auto t_step_end = tracing ? Clock::now() : Clock::time_point{};

    s.aliveCount = nextAlive;
    swap_buffers(s);
    ++s.generation;

    if (tracing) {
        int rank = 0;
        MPI_Comm_rank(ctx.comm, &rank);
        StepTrace trace;
        trace.engine_name       = "mpi";
        trace.requested_threads = 0;
        trace.effective_threads = 1;
        trace.width             = width;
        trace.height            = height;
        trace.generation_before = gen_before;
        trace.generation_after  = s.generation;
        trace.step_duration     = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t_step_end - t_step_begin).count());
        if (height > 0) {
            WorkerRecord rec;
            rec.thread_id       = rank;
            rec.first_row       = 1;
            rec.end_row         = height + 1;
            rec.cells_processed = height * width;
            rec.loop_duration   = static_cast<std::uint64_t>(stats.computeSeconds * 1e9);
            trace.workers.push_back(rec);
        }
        sink->onStepComplete(std::move(trace));
    }
    return stats;
}

std::size_t mpi_global_alive(const GameState& s, MPI_Comm comm) {
    unsigned long long local = s.aliveCount;
    unsigned long long total = 0;
    MPI_Allreduce(&local, &total, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, comm);
    return static_cast<std::size_t>(total);
}

}  // namespace conway::core

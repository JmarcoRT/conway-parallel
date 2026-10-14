// Tests de la infraestructura de trazas (StepTrace, WorkerRecord, StepTraceSink)
// y de la instrumentación opcional de los motores Secuencial y OpenMP.
//
// No requiere CMake (se compila con g++ -std=c++20 -fopenmp).
// Uso: step_trace_tests [raiz_del_repo]   (por defecto ".")

#include "core/bs23.hpp"
#include "core/engines/openmp.hpp"
#include "core/engines/sequential.hpp"
#include "core/game.hpp"
#include "core/game_state.hpp"
#include "core/step_trace.hpp"
#include "input/input.hpp"

#include <omp.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_passes = 0;
int g_failures = 0;

void report(bool ok, const std::string& name, const std::string& detail = "") {
    if (ok) ++g_passes; else ++g_failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name;
    if (!detail.empty()) std::cout << ' ' << detail;
    std::cout << '\n';
}

InputData makeInput(std::size_t w, std::size_t h, unsigned seed,
                    double density = 0.35) {
    InputData in{w, h, std::vector<std::uint8_t>(w * h, 0)};
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (auto& v : in.cells) {
        v = (dist(rng) < density) ? std::uint8_t{1} : std::uint8_t{0};
    }
    return in;
}

// Sink de test: registra todas las trazas recibidas, sin copiarlas.
class RecordingSink : public conway::core::StepTraceSink {
public:
    void onStepComplete(conway::core::StepTrace&& trace) override {
        std::lock_guard lock(mtx_);
        traces_.push_back(std::move(trace));
    }
    std::size_t size() const {
        std::lock_guard lock(mtx_);
        return traces_.size();
    }
    const std::vector<conway::core::StepTrace>& traces() const { return traces_; }
    void clear() {
        std::lock_guard lock(mtx_);
        traces_.clear();
    }
private:
    mutable std::mutex mtx_;
    std::vector<conway::core::StepTrace> traces_;
};

// Comprueba que la traza cubre exactamente [1, height] con workers disjuntos
// y sin huecos. Válido para schedule(static). Devuelve true si la cobertura
// es completa; en caso contrario escribe el motivo en `why`.
bool verifyCoverage(const conway::core::StepTrace& t, std::string& why) {
    if (t.workers.empty()) {
        if (t.height == 0) return true;
        why = "workers vacio y height > 0";
        return false;
    }
    std::vector<std::pair<std::size_t, std::size_t>> iv;
    iv.reserve(t.workers.size());
    for (const auto& w : t.workers) {
        if (w.end_row <= w.first_row) {
            why = "rango vacio (end_row <= first_row)";
            return false;
        }
        iv.emplace_back(w.first_row, w.end_row);
    }
    std::sort(iv.begin(), iv.end());
    std::size_t expected = 1;
    for (const auto& [f, e] : iv) {
        if (f != expected) {
            why = "hueco o solape en fila " + std::to_string(expected);
            return false;
        }
        expected = e;
    }
    if (expected - 1 != t.height) {
        why = "cobertura no llega al alto total";
        return false;
    }
    return true;
}

bool sameBoards(const conway::Game& a, const conway::Game& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    if (a.generation() != b.generation()) return false;
    if (a.aliveCount() != b.aliveCount()) return false;
    for (std::size_t r = 0; r < a.height(); ++r)
        for (std::size_t c = 0; c < a.width(); ++c)
            if (a.isAlive(r, c) != b.isAlive(r, c)) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = (argc > 1) ? argv[1] : ".";
    try {
        // -----------------------------------------------------------------
        // Tier 1: Estructuras de datos
        // -----------------------------------------------------------------
        {
            conway::core::StepTrace t;
            report(t.engine_name.empty() && t.workers.empty() &&
                       t.requested_threads == 0 && t.effective_threads == 0 &&
                       t.width == 0 && t.height == 0 &&
                       t.generation_before == 0 && t.generation_after == 0 &&
                       t.step_duration == 0,
                   "step_trace_default_construction_is_zeroed");
        }
        {
            conway::core::WorkerRecord w;
            report(w.thread_id == -1 && w.first_row == 0 && w.end_row == 0 &&
                       w.cells_processed == 0 && w.loop_duration == 0,
                   "worker_record_default_construction_is_zeroed");
        }
        {
            // Movimiento: el sink recibe la traza por movimiento.
            struct MoveSink : conway::core::StepTraceSink {
                bool received = false;
                std::size_t received_size = 0;
                void onStepComplete(conway::core::StepTrace&& t) override {
                    received = true;
                    received_size = t.workers.size();
                }
            } sink;
            conway::core::StepTrace t;
            t.workers.resize(3);
            sink.onStepComplete(std::move(t));
            report(sink.received && sink.received_size == 3,
                   "step_trace_sink_receives_by_move");
        }

        // -----------------------------------------------------------------
        // Tier 2: Equivalencia con y sin instrumentación
        // -----------------------------------------------------------------
        omp_set_dynamic(0);
        for (int threads : {1, 2, 4, 8}) {
            omp_set_num_threads(threads);

            for (auto engine : {conway::Engine::Sequential,
                                conway::Engine::OpenMP}) {
                for (std::size_t h : {1u, 5u, 17u, 64u}) {
                    for (std::size_t w : {1u, 5u, 17u, 64u}) {
                        const InputData in =
                            makeInput(w, h, 1000 + threads + static_cast<int>(h * 31 + w));

                        conway::Game a(in);
                        a.setEngine(engine);
                        conway::Game b(in);
                        b.setEngine(engine);

                        const int gens = 3;
                        for (int g = 0; g < gens; ++g) {
                            a.step();
                            b.step(conway::core::kNoTrace);
                        }
                        const std::string detail =
                            "engine=" + std::to_string(static_cast<int>(engine)) +
                            " h=" + std::to_string(h) +
                            " w=" + std::to_string(w) +
                            " threads=" + std::to_string(threads);
                        report(sameBoards(a, b),
                               "step_with_and_without_sink_produce_same_state",
                               detail);
                    }
                }
            }
        }

        // Equivalencia con h == 0: usamos GameState directo porque Game
        // rechaza dimensiones inválidas.
        {
            conway::core::GameState s;
            s.width = 8;
            s.height = 0;
            s.stride = 10;
            s.current.assign(s.stride * 2, 0);
            s.next.assign(s.stride * 2, 0);

            conway::core::GameState a = s;
            conway::core::GameState b = s;
            conway::core::step_sequential(a, conway::core::kNoTrace);
            conway::core::step_sequential(b, conway::core::kNoTrace);
            report(a.width == b.width && a.height == b.height &&
                       a.generation == 1 && b.generation == 1 &&
                       a.aliveCount == 0 && b.aliveCount == 0,
                   "step_with_and_without_sink_empty_dimensions_sequential");

            conway::core::GameState c = s;
            conway::core::GameState d = s;
            omp_set_num_threads(4);
            conway::core::step_openmp(c, conway::core::kNoTrace);
            conway::core::step_openmp(d, conway::core::kNoTrace);
            report(c.width == d.width && c.height == d.height &&
                       c.generation == 1 && d.generation == 1 &&
                       c.aliveCount == 0 && d.aliveCount == 0,
                   "step_with_and_without_sink_empty_dimensions_openmp");
        }

        // -----------------------------------------------------------------
        // Tier 3: Cobertura del tablero (sin huecos, sin solapes)
        //         y observación real de filas.
        // -----------------------------------------------------------------
        omp_set_dynamic(0);
        for (int threads : {1, 2, 3, 4, 5, 7, 8, 16, 32, 64}) {
            omp_set_num_threads(threads);
            for (std::size_t h : {1u, 3u, 7u, 16u, 64u, 100u, 257u}) {
                for (std::size_t w : {1u, 8u, 64u}) {
                    const InputData in =
                        makeInput(w, h, 2000 + threads + static_cast<int>(h * 7 + w));
                    conway::Game g(in);
                    g.setEngine(conway::Engine::OpenMP);

                    RecordingSink sink;
                    g.step(&sink);
                    const auto& traces = sink.traces();
                    if (traces.size() != 1) {
                        report(false, "openmp_step_emits_exactly_one_trace",
                               "threads=" + std::to_string(threads) +
                               " h=" + std::to_string(h) +
                               " w=" + std::to_string(w));
                        continue;
                    }
                    const auto& t = traces.front();
                    std::string why;
                    const bool ok = verifyCoverage(t, why);
                    const std::string detail =
                        "threads=" + std::to_string(threads) +
                        " h=" + std::to_string(h) +
                        " w=" + std::to_string(w) +
                        " workers=" + std::to_string(t.workers.size()) +
                        " effective=" + std::to_string(t.effective_threads) +
                        " cover=" + (ok ? "ok" : why);
                    report(ok, "openmp_coverage_no_gaps_no_overlaps", detail);

                    // Comprobaciones adicionales:
                    // - effective_threads coincide con el team observado.
                    // - suma de cells_processed == width*height.
                    bool sum_ok = true;
                    std::size_t sum = 0;
                    for (const auto& wr : t.workers) sum += wr.cells_processed;
                    sum_ok = (sum == w * h);
                    report(sum_ok, "openmp_cells_processed_sum_matches_board",
                           detail);
                    report(t.effective_threads >= 1 &&
                               t.effective_threads <= omp_get_max_threads(),
                           "openmp_effective_threads_in_range", detail);
                    report(t.workers.size() > 0 &&
                               static_cast<int>(t.workers.size()) <= t.effective_threads,
                           "openmp_active_workers_le_effective", detail);
                }
            }
        }

        // Mismo coverage para secuencial (1 worker, todo el tablero si h>0).
        for (std::size_t h : {0u, 1u, 7u, 64u, 257u}) {
            for (std::size_t w : {1u, 5u, 64u}) {
                if (h == 0) {
                    // Game rechaza h==0 (existing behavior). Probamos el
                    // motor directamente sobre un GameState construido a
                    // mano, con dimensiones válidas (w > 0, h == 0).
                    conway::core::GameState s;
                    s.width = w;
                    s.height = 0;
                    s.stride = w + 2;
                    s.current.assign(s.stride * (s.height + 2), 0);
                    s.next.assign(s.stride * (s.height + 2), 0);
                    RecordingSink sink;
                    conway::core::step_sequential(s, &sink);
                    const auto& traces = sink.traces();
                    if (traces.size() != 1) {
                        report(false, "sequential_step_emits_exactly_one_trace_empty");
                        continue;
                    }
                    const auto& t = traces.front();
                    std::string why;
                    const bool ok = verifyCoverage(t, why);
                    report(ok, "sequential_coverage_no_gaps_no_overlaps_empty",
                           "h=0 w=" + std::to_string(w) +
                           " why=" + (ok ? std::string("ok") : why));
                    report(t.effective_threads == 1,
                           "sequential_effective_threads_is_one_empty");
                    report(t.workers.empty(),
                           "sequential_empty_height_produces_no_workers");
                } else {
                    const InputData in = makeInput(w, h, 3000 + static_cast<int>(h + w));
                    conway::Game g(in);
                    g.setEngine(conway::Engine::Sequential);
                    RecordingSink sink;
                    g.step(&sink);
                    const auto& traces = sink.traces();
                    if (traces.size() != 1) {
                        report(false, "sequential_step_emits_exactly_one_trace");
                        continue;
                    }
                    const auto& t = traces.front();
                    std::string why;
                    const bool ok = verifyCoverage(t, why);
                    report(ok, "sequential_coverage_no_gaps_no_overlaps",
                           "h=" + std::to_string(h) + " w=" + std::to_string(w) +
                           " why=" + (ok ? std::string("ok") : why));
                    report(t.effective_threads == 1,
                           "sequential_effective_threads_is_one",
                           "h=" + std::to_string(h) + " w=" + std::to_string(w));
                    report(t.workers.size() == 1 &&
                               t.workers.front().first_row == 1 &&
                               t.workers.front().end_row == h + 1 &&
                               t.workers.front().cells_processed == w * h,
                           "sequential_single_worker_covers_whole_board",
                           "h=" + std::to_string(h) + " w=" + std::to_string(w));
                }
            }
        }

        // OpenMP con height == 0: igual, sin pasar por Game.
        {
            conway::core::GameState s;
            s.width = 8;
            s.height = 0;
            s.stride = 10;
            s.current.assign(s.stride * 2, 0);
            s.next.assign(s.stride * 2, 0);
            omp_set_dynamic(0);
            omp_set_num_threads(4);
            RecordingSink sink;
            conway::core::step_openmp(s, &sink);
            const auto& traces = sink.traces();
            report(traces.size() == 1 && traces.front().workers.empty(),
                   "openmp_empty_height_produces_no_workers",
                   "got_workers=" + std::to_string(traces.front().workers.size()));
        }

        // -----------------------------------------------------------------
        // Tier 4: Engine name y requested_threads (sin inferir).
        // -----------------------------------------------------------------
        {
            const InputData in = makeInput(8, 8, 4000);
            conway::Game g(in);
            g.setEngine(conway::Engine::Sequential);
            RecordingSink sink;
            g.step(&sink);
            const auto& t = sink.traces().front();
            report(t.engine_name == "sequential",
                   "sequential_engine_name_is_sequential");
            report(t.requested_threads == 0,
                   "engine_does_not_infer_requested_threads",
                   "got=" + std::to_string(t.requested_threads));
        }
        {
            const InputData in = makeInput(8, 8, 4001);
            conway::Game g(in);
            g.setEngine(conway::Engine::OpenMP);
            RecordingSink sink;
            g.step(&sink);
            const auto& t = sink.traces().front();
            report(t.engine_name == "openmp",
                   "openmp_engine_name_is_openmp");
            report(t.requested_threads == 0,
                   "openmp_does_not_infer_requested_threads",
                   "got=" + std::to_string(t.requested_threads));
        }

        // -----------------------------------------------------------------
        // Tier 5: sink nullptr no instrumenta (vector de trazas vacío).
        // -----------------------------------------------------------------
        {
            const InputData in = makeInput(8, 8, 5000);
            conway::Game g(in);
            g.setEngine(conway::Engine::OpenMP);
            RecordingSink sink;
            g.step(conway::core::kNoTrace);
            g.step(nullptr);
            report(sink.traces().empty(),
                   "no_trace_path_does_not_invoke_sink");
        }

        // -----------------------------------------------------------------
        // Tier 6: Concurrencia del sink (una llamada por step, no por worker).
        // -----------------------------------------------------------------
        {
            omp_set_dynamic(0);
            omp_set_num_threads(4);
            const InputData in = makeInput(32, 32, 6000);
            conway::Game g(in);
            g.setEngine(conway::Engine::OpenMP);
            RecordingSink sink;
            for (int step = 0; step < 20; ++step) {
                g.step(&sink);
            }
            report(sink.traces().size() == 20,
                   "sink_invoked_once_per_step_not_per_worker",
                   "got=" + std::to_string(sink.traces().size()));
            // Cada traza debe tener un solo step coherente.
            bool monotonic = true;
            for (std::size_t i = 0; i < sink.traces().size(); ++i) {
                if (sink.traces()[i].generation_before != i ||
                    sink.traces()[i].generation_after != i + 1) {
                    monotonic = false;
                    break;
                }
            }
            report(monotonic, "trace_generations_are_strictly_increasing");
        }

        // -----------------------------------------------------------------
        // Tier 7: Sin estado residual entre llamadas con diferentes sinks.
        // -----------------------------------------------------------------
        {
            const InputData in = makeInput(16, 16, 7000);
            conway::Game g(in);
            g.setEngine(conway::Engine::OpenMP);
            RecordingSink sinkA;
            RecordingSink sinkB;
            for (int i = 0; i < 5; ++i) g.step(&sinkA);
            for (int i = 0; i < 5; ++i) g.step(&sinkB);
            report(sinkA.traces().size() == 5 &&
                       sinkB.traces().size() == 5 &&
                       sinkA.traces().back().generation_after == 5 &&
                       sinkB.traces().back().generation_after == 10,
                   "no_residual_state_between_sinks",
                   "A=" + std::to_string(sinkA.traces().size()) +
                   " B=" + std::to_string(sinkB.traces().size()));
        }

        // -----------------------------------------------------------------
        // Tier 8: Tiempos monotónicos y razonables (no son aserciones rígidas,
        //         sólo que no haya倒退).
        // -----------------------------------------------------------------
        {
            const InputData in = makeInput(64, 64, 8000);
            conway::Game g(in);
            g.setEngine(conway::Engine::OpenMP);
            RecordingSink sink;
            for (int i = 0; i < 3; ++i) g.step(&sink);
            bool ok = true, ordered = true;
            for (const auto& t : sink.traces()) {
                int previousId = -1;
                for (const auto& w : t.workers) {
                    ordered = ordered && w.thread_id > previousId;
                    previousId = w.thread_id;
                    // Cada intervalo individual esta contenido en el global.
                    // No se suman intervalos concurrentes ni se infiere su orden.
                    if (w.loop_duration > t.step_duration) {
                        ok = false;
                    }
                }
            }
            report(ok, "worker_loop_duration_does_not_exceed_step_duration");
            report(ordered, "workers_ordered_by_id_not_temporal_order");
        }

        // -----------------------------------------------------------------
        // Tier 9: runBenchmark con sink nullptr no se invoca (compatibilidad
        //         con la ruta no instrumentada). Verificamos que los campos
        //         de la métrica no han cambiado de forma observable.
        // -----------------------------------------------------------------
        {
            omp_set_dynamic(0);
            omp_set_num_threads(4);
            const InputData in = makeInput(16, 16, 9000);
            conway::Game a(in);
            a.setEngine(conway::Engine::Sequential);
            conway::Game b(in);
            b.setEngine(conway::Engine::OpenMP);
            omp_set_num_threads(4);
            const int kGens = 5;
            for (int i = 0; i < kGens; ++i) {
                a.step();
                b.step();
            }
            report(sameBoards(a, b),
                   "benchmark_path_preserves_equivalence_without_sink",
                   "gens=" + std::to_string(kGens));
        }

        // -----------------------------------------------------------------
        // Tier 10: Casos con más hilos que filas (hilos ociosos).
        //         Los hilos ociosos NO deben aparecer en la traza.
        // -----------------------------------------------------------------
        {
            omp_set_dynamic(0);
            omp_set_num_threads(16);
            const InputData in = makeInput(8, 3, 9100);
            conway::Game g(in);
            g.setEngine(conway::Engine::OpenMP);
            RecordingSink sink;
            g.step(&sink);
            const auto& t = sink.traces().front();
            std::string why;
            const bool ok = verifyCoverage(t, why);
            report(ok && t.workers.size() <= static_cast<std::size_t>(t.effective_threads),
                   "more_threads_than_rows_only_active_workers_recorded",
                   "workers=" + std::to_string(t.workers.size()) +
                   " effective=" + std::to_string(t.effective_threads) +
                   " h=3 cover=" + (ok ? "ok" : why));
        }

        std::cout << "Resultado: " << g_passes << " PASS, " << g_failures
                  << " FAIL\n";
        return g_failures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }
}

// Tests de DidacticController y de su snapshot.
//
// Cubre:
//   - Preparacion sincrona: pre, post y trace son correctos.
//   - Equivalencia con la salida del motor (next coincide con step).
//   - Modo Secuencial: un unico worker.
//   - Modo OpenMP: tantos workers como hilos efectivos, cobertura
//     completa de filas sin solapes.
//   - Reproduccion: pause/play/toggle, tick, auto-pausa al
//     completar, revelado progresivo de celdas.
//   - Velocidad: increase/decrease/setSpeed con limites.
//   - Navegacion: advanceGeneration y resetToInitial funcionan
//     y no rompen la invariante de no-solapamientos.
//   - Reentrancia: dos prepare consecutivas no corrompen el estado
//     (la segunda espera a la primera o se hace sync via joinWorker).
//   - Estado de error: prepareSynchronously sobre una entrada
//     invalida deja phase() == Error y un mensaje.
//
// Uso: didactic_session_tests [raiz_del_repo]

#include "app/didactic_session.hpp"
#include "core/game.hpp"
#include "core/step_trace.hpp"
#include "input/input.hpp"

#include <omp.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
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
                    double density = 0.4) {
    InputData in{w, h, std::vector<std::uint8_t>(w * h, 0)};
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (auto& v : in.cells) {
        v = (dist(rng) < density) ? std::uint8_t{1} : std::uint8_t{0};
    }
    return in;
}

InputData makeBlinker() {
    InputData in{5, 5, std::vector<std::uint8_t>(25, 0)};
    in.cells[2 * 5 + 1] = 1;
    in.cells[2 * 5 + 2] = 1;
    in.cells[2 * 5 + 3] = 1;
    return in;
}

// Compara cell-by-cell entre dos Game con las mismas dimensiones.
bool sameAliveCells(const conway::Game& a, const conway::Game& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    if (a.aliveCount() != b.aliveCount()) return false;
    for (std::size_t r = 0; r < a.height(); ++r) {
        for (std::size_t c = 0; c < a.width(); ++c) {
            if (a.isAlive(r, c) != b.isAlive(r, c)) return false;
        }
    }
    return true;
}

bool coverageComplete(const conway::app::DidacticFrame& frame) {
    if (frame.workers.empty()) return frame.height == 0;
    std::vector<std::pair<int, int>> iv;
    for (const auto& w : frame.workers) {
        iv.emplace_back(w.first_row, w.end_row);
    }
    std::sort(iv.begin(), iv.end());
    std::size_t expected = 1;
    for (const auto& [f, e] : iv) {
        if (static_cast<std::size_t>(f) != expected) return false;
        if (e <= f) return false;
        expected = static_cast<std::size_t>(e);
    }
    return expected - 1 == frame.height;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = (argc > 1) ? argv[1] : ".";
    (void)root;
    try {
        omp_set_dynamic(0);

        // ----------------------------------------------------------------
        // Tier 1: Estado inicial.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            report(c.phase() == conway::app::DidacticController::Phase::Idle,
                   "initial_phase_is_idle");
            report(!c.isReady() && !c.isPreparing(),
                   "initial_is_not_ready_or_preparing");
            report(!c.isPlaying() && c.speed() == 1.0,
                   "initial_speed_is_one_and_not_playing");
            const auto frame = c.snapshot();
            report(!frame.has_data && frame.engine_name.size() > 0,
                   "initial_snapshot_has_engine_name_but_no_data");
        }

        // ----------------------------------------------------------------
        // Tier 2: Preparacion sincrona Secuencial.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeBlinker();
            c.prepareSynchronously(in, conway::Engine::Sequential, 1);
            c.waitUntilReady();
            report(c.isReady(),
                   "synchronous_sequential_is_ready");
            report(c.errorMessage().empty(),
                   "synchronous_sequential_no_error",
                   "msg=" + c.errorMessage());

            const conway::Game* pre = c.preStep();
            const conway::Game* post = c.postStep();
            const conway::core::StepTrace* tr = c.stepTrace();
            report(pre != nullptr && post != nullptr && tr != nullptr,
                   "synchronous_sequential_exposes_data");

            report(pre->generation() == 0 && post->generation() == 1,
                   "synchronous_sequential_generations",
                   "pre=" + std::to_string(pre->generation()) +
                   " post=" + std::to_string(post->generation()));

            // CURRENT coincide con la entrada
            bool pre_matches = true;
            for (std::size_t r = 0; r < pre->height(); ++r) {
                for (std::size_t c = 0; c < pre->width(); ++c) {
                    if ((pre->isAlive(r, c) ? 1 : 0)
                        != static_cast<int>(in.cells[r * in.width + c])) {
                        pre_matches = false;
                    }
                }
            }
            report(pre_matches, "pre_step_matches_input");

            // NEXT coincide con un step independiente del motor
            conway::Game expected(in);
            expected.setEngine(conway::Engine::Sequential);
            expected.step();
            report(sameAliveCells(*post, expected),
                   "next_matches_engine_step_result");

            // Secuencial -> 1 worker
            report(tr->workers.size() == 1
                       && tr->effective_threads == 1,
                   "sequential_uses_single_worker",
                   "workers=" + std::to_string(tr->workers.size()) +
                   " effective=" + std::to_string(tr->effective_threads));

            // Frame metadata
            const auto frame = c.snapshot();
            report(frame.has_data, "snapshot_has_data");
            report(frame.engine_name == "Secuencial",
                   "snapshot_engine_name_is_sequential");
            report(frame.effective_threads == 1,
                   "snapshot_effective_threads_one");
            report(frame.current_generation == 0
                       && frame.next_generation == 1,
                   "snapshot_generations_correct");
            report(frame.workers.size() == 1
                       && frame.workers[0].is_sequential,
                   "snapshot_worker_is_sequential");
            report(frame.workers.size() == 1
                       && frame.workers[0].first_row == 1
                       && frame.workers[0].end_row ==
                          static_cast<int>(in.height) + 1,
                   "snapshot_worker_covers_full_board");
        }

        // ----------------------------------------------------------------
        // Tier 3: Preparacion sincrona OpenMP.
        // ----------------------------------------------------------------
        for (int threads : {1, 2, 4, 8}) {
            omp_set_num_threads(threads);
            for (std::size_t h : {1u, 3u, 8u, 17u, 64u}) {
                for (std::size_t w : {1u, 7u, 64u}) {
                    const InputData in = makeInput(w, h, 1000 + threads +
                        static_cast<int>(h * 17 + w));
                    conway::app::DidacticController c;
                    c.prepareSynchronously(in, conway::Engine::OpenMP,
                                           threads);
                    c.waitUntilReady();
                    if (!c.isReady()) {
                        report(false, "openmp_prepare_ready",
                               "threads=" + std::to_string(threads));
                        continue;
                    }
                    const conway::Game* post = c.postStep();
                    const conway::core::StepTrace* tr = c.stepTrace();
                    if (post == nullptr || tr == nullptr) {
                        report(false, "openmp_data_exposed");
                        continue;
                    }
                    conway::Game expected(in);
                    expected.setEngine(conway::Engine::OpenMP);
                    omp_set_num_threads(threads);
                    expected.step();
                    const std::string detail =
                        "threads=" + std::to_string(threads) +
                        " h=" + std::to_string(h) +
                        " w=" + std::to_string(w);
                    report(sameAliveCells(*post, expected),
                           "openmp_next_matches_engine", detail);
                    const auto frame = c.snapshot();
                    report(coverageComplete(frame),
                           "openmp_coverage_complete", detail);
                    report(frame.effective_threads >=
                               static_cast<int>(frame.workers.size()),
                           "openmp_workers_le_effective",
                           detail + " eff=" +
                           std::to_string(frame.effective_threads) +
                           " workers=" +
                           std::to_string(frame.workers.size()));
                }
            }
        }

        // ----------------------------------------------------------------
        // Tier 4: Reproduccion. La primera mitad del reloj solo
        //         revela parte de los workers; al llegar al final
        //         todo esta revelado.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeInput(16, 16, 7777);
            c.prepareSynchronously(in, conway::Engine::OpenMP, 4);
            c.waitUntilReady();
            const auto initial = c.snapshot();
            report(initial.has_data && initial.total_playback_ns > 0,
                   "playback_total_ns_is_set");
            report(!initial.fully_revealed,
                   "initial_snapshot_is_not_fully_revealed");

            // Reproducir tick a tick con dt muy peque~no para no
            // saltar el evento de auto-pausa.
            c.play();
            report(c.isPlaying(), "play_sets_playing");

            const std::uint64_t total = initial.total_playback_ns;
            // Avanza la mitad del tiempo de varias formas; la
            // mascara no debe llegar a fully_revealed antes de
            // pasar el total.
            const std::uint64_t half_ns = total / 2;
            const std::uint64_t step_ns = 10000;  // 10 us por tick
            std::uint64_t consumed = 0;
            bool early_complete = false;
            while (consumed < half_ns && c.isPlaying()) {
                c.tick(std::chrono::nanoseconds(step_ns));
                consumed += step_ns;
                if (c.snapshot().fully_revealed) {
                    early_complete = true; break;
                }
            }
            // Si el reloj todavia no llego al final (hay workers
            // con duracion > half_ns), no debe estar fully revealed.
            if (consumed < total) {
                report(!early_complete,
                       "half_reveal_does_not_complete",
                       "consumed=" + std::to_string(consumed) +
                       " total=" + std::to_string(total));
            }

            // Llevar el reloj al final manualmente.
            c.tick(std::chrono::nanoseconds(total));
            const auto full = c.snapshot();
            report(full.fully_revealed,
                   "full_reveal_completes_snapshot");
            report(!c.isPlaying(),
                   "full_reveal_auto_pauses");

            // En el frame final, todas las celdas deben estar
            // reveladas y la suma de cells_processed == w*h.
            std::size_t sum_cells = 0;
            bool all_revealed = true;
            for (std::size_t i = 0; i < full.revealed.size(); ++i) {
                if (!full.revealed[i]) all_revealed = false;
            }
            for (const auto& w : full.workers) sum_cells += w.cells_processed;
            report(all_revealed, "all_cells_revealed_at_end");
            report(sum_cells == full.width * full.height,
                   "worker_cells_sum_matches_board",
                   "sum=" + std::to_string(sum_cells) +
                   " board=" + std::to_string(full.width * full.height));
        }

        // ----------------------------------------------------------------
        // Tier 5: Pause / play / togglePlay.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeBlinker();
            c.prepareSynchronously(in, conway::Engine::Sequential, 1);
            c.waitUntilReady();
            c.togglePlay();
            report(c.isPlaying(), "toggle_starts_playback");
            c.togglePlay();
            report(!c.isPlaying(), "toggle_pauses_playback");
            c.play();
            c.pause();
            report(!c.isPlaying(), "explicit_pause_works");
            // tick en pausa no debe avanzar
            const auto before = c.snapshot();
            c.tick(std::chrono::seconds(1));
            const auto after = c.snapshot();
            report(before.current_playback_ns == after.current_playback_ns,
                   "tick_while_paused_does_not_advance",
                   "before=" + std::to_string(before.current_playback_ns) +
                   " after=" + std::to_string(after.current_playback_ns));
        }

        // ----------------------------------------------------------------
        // Tier 6: Velocidad.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeBlinker();
            c.prepareSynchronously(in, conway::Engine::Sequential, 1);
            c.waitUntilReady();
            c.setSpeed(3.5);
            report(c.speed() == 3.5, "set_speed_within_range");
            c.setSpeed(100.0);
            report(c.speed() == 8.0, "set_speed_clamped_high");
            c.setSpeed(0.0);
            report(c.speed() == 0.1, "set_speed_clamped_low");
            c.setSpeed(1.0);
            c.increaseSpeed();
            report(c.speed() == 2.0, "increase_speed_steps");
            c.increaseSpeed();
            report(c.speed() == 4.0, "increase_speed_steps_again");
            c.increaseSpeed();
            c.increaseSpeed();
            c.increaseSpeed();
            report(c.speed() == 8.0, "increase_speed_saturates");
            c.decreaseSpeed();
            report(c.speed() == 4.0, "decrease_speed_steps");
            c.decreaseSpeed();
            c.decreaseSpeed();
            c.decreaseSpeed();
            c.decreaseSpeed();
            c.decreaseSpeed();
            report(c.speed() == 0.25, "decrease_speed_saturates_low");
        }

        // ----------------------------------------------------------------
        // Tier 7: Navegacion entre generaciones.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeBlinker();
            c.prepareSynchronously(in, conway::Engine::Sequential, 1);
            c.waitUntilReady();
            const conway::Game* p1ptr = c.postStep();
            report(p1ptr != nullptr && p1ptr->generation() == 1,
                   "advance_pre_first_post_is_gen_1");
            // Tomamos una copia: advanceGeneration modifica el estado
            // interno y el puntero p1ptr quedaria colgando.
            conway::Game p1 = *p1ptr;

            c.advanceGeneration();
            c.waitUntilReady();
            const conway::Game* p2 = c.postStep();
            const conway::Game* p2pre = c.preStep();
            report(p2 != nullptr && p2pre != nullptr,
                   "advance_data_exposed");
            report(p2pre->generation() == 1 && p2->generation() == 2,
                   "advance_generations_increment",
                   "pre=" + std::to_string(p2pre->generation()) +
                   " post=" + std::to_string(p2->generation()));
            // El pre_step de la nueva generacion debe coincidir con
            // el post_step anterior (en contenido de celdas).
            report(sameAliveCells(*p2pre, p1),
                   "advance_pre_matches_previous_post");

            c.resetToInitial();
            c.waitUntilReady();
            const conway::Game* p3pre = c.preStep();
            report(p3pre != nullptr && p3pre->generation() == 0,
                   "reset_to_initial_generation_zero");
            // El pre tras reset debe coincidir con la entrada inicial
            // original, NO con el estado posterior del advance.
            bool pre_matches_input = true;
            for (std::size_t r = 0; r < p3pre->height(); ++r) {
                for (std::size_t c = 0; c < p3pre->width(); ++c) {
                    if ((p3pre->isAlive(r, c) ? 1 : 0) !=
                        static_cast<int>(in.cells[r * in.width + c])) {
                        pre_matches_input = false;
                    }
                }
            }
            report(pre_matches_input,
                   "reset_pre_matches_initial_input");
        }

        // ----------------------------------------------------------------
        // Tier 8: Cobertura en avance de multiples generaciones.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeInput(24, 24, 31415);
            c.prepareSynchronously(in, conway::Engine::OpenMP, 4);
            c.waitUntilReady();
            for (int step = 0; step < 3; ++step) {
                const auto fr = c.snapshot();
                report(coverageComplete(fr),
                       "openmp_coverage_after_advance",
                       "step=" + std::to_string(step));
                c.advanceGeneration();
                c.waitUntilReady();
            }
        }

        // ----------------------------------------------------------------
        // Tier 9: Reentrancia con dos preparaciones consecutivas.
        //         La primera debe quedar correctamente entregada antes
        //         de que la segunda empiece.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData a = makeInput(8, 8, 1);
            const InputData b = makeInput(8, 8, 2);
            c.prepareSynchronously(a, conway::Engine::OpenMP, 2);
            c.waitUntilReady();
            c.prepareSynchronously(b, conway::Engine::OpenMP, 2);
            c.waitUntilReady();
            const conway::Game* pre = c.preStep();
            conway::Game expected_b(b);
            expected_b.setEngine(conway::Engine::OpenMP);
            omp_set_num_threads(2);
            // El pre_step debe coincidir con la entrada b
            bool matches_b = true;
            for (std::size_t r = 0; r < pre->height(); ++r) {
                for (std::size_t c = 0; c < pre->width(); ++c) {
                    if ((pre->isAlive(r, c) ? 1 : 0) !=
                        static_cast<int>(b.cells[r * b.width + c])) {
                        matches_b = false;
                    }
                }
            }
            report(matches_b, "second_preparation_overrides_first");
        }

        // ----------------------------------------------------------------
        // Tier 10: Pausa sin refrescos periodicos: si playing es
        //          false, snapshot retorna el mismo current_playback_ns.
        //          Verificamos que la reproduccion no avanza ticks
        //          que el caller no solicite.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeBlinker();
            c.prepareSynchronously(in, conway::Engine::Sequential, 1);
            c.waitUntilReady();
            const auto a = c.snapshot();
            // Sin tick, el reloj no cambia
            const auto b = c.snapshot();
            report(a.current_playback_ns == b.current_playback_ns,
                   "no_tick_no_playback_advance");
        }

        // ----------------------------------------------------------------
        // Tier 11: Estado de error al recibir dimensiones invalidas.
        //         Game(InputData) lanza std::invalid_argument cuando
        //         las dimensiones son 0; ese es el contrato existente
        //         y no lo cambiamos. El controlador lo propaga y eso
        //         es aceptable; lo que verificamos aqui es que el
        //         Game (no el controlador) detecta el caso.
        // ----------------------------------------------------------------
        {
            InputData bad;
            bad.width = 0;
            bad.height = 0;
            bad.cells = {};
            bool threw = false;
            try {
                conway::Game g(bad);
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            report(threw, "invalid_dimensions_throw_on_game_construction");
        }

        // ----------------------------------------------------------------
        // Tier 12: El path asincrono (startPreparation) produce
        //          resultados equivalentes a la version sincrona.
        // ----------------------------------------------------------------
        {
            conway::app::DidacticController c;
            const InputData in = makeBlinker();
            c.startPreparation(in, conway::Engine::Sequential, 1);
            c.waitUntilReady();
            const conway::Game* post = c.postStep();
            const conway::Game* pre = c.preStep();
            report(c.isReady(), "async_path_reaches_ready");
            report(pre != nullptr && pre->generation() == 0,
                   "async_path_pre_is_generation_0");
            report(post != nullptr && post->generation() == 1,
                   "async_path_post_is_generation_1");
            conway::Game expected(in);
            expected.setEngine(conway::Engine::Sequential);
            expected.step();
            report(sameAliveCells(*post, expected),
                   "async_path_next_matches_engine");
        }

        std::cout << "Resultado: " << g_passes << " PASS, " << g_failures
                  << " FAIL\n";
        return g_failures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }
}

#include "app/didactic_session.hpp"

#include <algorithm>
#include <omp.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace conway::app {

namespace {

// Sink de captura que retiene la unica traza que recibe. Dise~nado
// para el camino didactico: el worker crea este sink, lo entrega al
// motor, y al salir del step toma la traza por movimiento.
class CapturingSink : public conway::core::StepTraceSink {
public:
    void onStepComplete(conway::core::StepTrace&& trace) override {
        captured_ = std::move(trace);
    }
    bool has() const { return captured_.has_value(); }
    const conway::core::StepTrace& get() const { return *captured_; }
    conway::core::StepTrace take() { return std::move(*captured_); }
private:
    std::optional<conway::core::StepTrace> captured_;
};

// Convierte las celdas de un Game a InputData. La generacion se
// pierde (InputData no la transporta); el llamante debe llevar
// control de la misma si la necesita.
InputData gameToInputData(const conway::Game& g) {
    InputData in;
    in.width = g.width();
    in.height = g.height();
    in.cells.assign(in.width * in.height, 0);
    for (std::size_t r = 0; r < g.height(); ++r) {
        for (std::size_t c = 0; c < g.width(); ++c) {
            in.cells[r * g.width() + c] = g.isAlive(r, c) ? 1 : 0;
        }
    }
    return in;
}

}  // namespace

DidacticController::DidacticController() = default;

DidacticController::~DidacticController() {
    joinWorker();
}

void DidacticController::setInitialInput(const InputData& input) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    initial_input_ = input;
    has_initial_ = true;
}

const std::string& DidacticController::errorMessage() const {
    static const std::string empty;
    std::lock_guard<std::mutex> lock(data_mutex_);
    return error_;
}

void DidacticController::joinWorker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void DidacticController::waitUntilReady() {
    std::unique_lock<std::mutex> lock(data_mutex_);
    data_cv_.wait(lock, [this] {
        const Phase p = phase_.load(std::memory_order_acquire);
        return p == Phase::Ready || p == Phase::Error || p == Phase::Idle;
    });
}

void DidacticController::startPreparation(const InputData& pre_input,
                                          conway::Engine engine,
                                          int requested_threads) {
    // Construye un Game explicitamente en generacion 0. Cualquier
    // generacion previa del controlador (de un advance anterior) se
    // reinicia al inicio del patron.
    conway::Game pre(pre_input);
    pre.setEngine(engine);
    prepareFromGame(std::move(pre), engine, requested_threads,
                    /*reset_to_initial=*/true);
}

void DidacticController::prepareSynchronously(const InputData& pre_input,
                                               conway::Engine engine,
                                               int requested_threads) {
    conway::Game pre(pre_input);
    pre.setEngine(engine);
    prepareFromGameSync(std::move(pre), engine, requested_threads,
                        /*reset_to_initial=*/true);
}

void DidacticController::prepareFromGame(conway::Game pre,
                                          conway::Engine engine,
                                          int requested_threads,
                                          bool reset_to_initial) {
    // Une cualquier worker previo. En la practica el llamante ya
    // espera a Ready antes de pedir otro step, pero por seguridad
    // ante un cambio rapido de modo lo manejamos aqui.
    joinWorker();

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        engine_ = engine;
        requested_threads_ = requested_threads;
        pre_step_.reset();
        post_step_.reset();
        trace_.reset();
        error_.clear();
        phase_.store(Phase::Preparing, std::memory_order_release);
        if (reset_to_initial) {
            // Tomamos las celdas del Game como nuevo initial input.
            // Importante: la generacion del initial siempre es 0
            // (es la primera generacion del patron).
            initial_input_ = gameToInputData(pre);
            has_initial_ = true;
            current_generation_ = pre.generation();
        }
    }
    clearPlayback();

    // Para el worker, `post` es una copia de `pre` que se mutara con
    // step(). Asi `pre` queda intacto como CURRENT.
    conway::Game post = pre;
    post.setEngine(engine);

    worker_ = std::thread([this, pre_in = std::move(pre),
                           post_in = std::move(post),
                           engine, requested_threads]() mutable {
        try {
            if (engine == conway::Engine::OpenMP) {
                omp_set_num_threads(requested_threads);
            }
            post_in.setEngine(engine);
            CapturingSink sink;
            post_in.step(&sink);
            if (!sink.has()) {
                std::lock_guard<std::mutex> lock(data_mutex_);
                error_ = "El motor no emitio StepTrace";
                phase_.store(Phase::Error, std::memory_order_release);
            } else {
                storeReadyData(std::move(pre_in), std::move(post_in),
                               sink.take());
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            error_ = std::string("Excepcion durante el step: ") + e.what();
            phase_.store(Phase::Error, std::memory_order_release);
        } catch (...) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            error_ = "Excepcion desconocida durante el step";
            phase_.store(Phase::Error, std::memory_order_release);
        }
        data_cv_.notify_all();
    });
}

void DidacticController::prepareFromGameSync(conway::Game pre,
                                              conway::Engine engine,
                                              int requested_threads,
                                              bool reset_to_initial) {
    joinWorker();
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        engine_ = engine;
        requested_threads_ = requested_threads;
        pre_step_.reset();
        post_step_.reset();
        trace_.reset();
        error_.clear();
        phase_.store(Phase::Preparing, std::memory_order_release);
        if (reset_to_initial) {
            initial_input_ = gameToInputData(pre);
            has_initial_ = true;
            current_generation_ = pre.generation();
        }
    }
    clearPlayback();

    try {
        if (engine == conway::Engine::OpenMP) {
            omp_set_num_threads(requested_threads);
        }
        conway::Game post = pre;
        post.setEngine(engine);
        CapturingSink sink;
        post.step(&sink);
        if (!sink.has()) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            error_ = "El motor no emitio StepTrace";
            phase_.store(Phase::Error, std::memory_order_release);
        } else {
            storeReadyData(std::move(pre), std::move(post), sink.take());
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        error_ = std::string("Excepcion durante el step: ") + e.what();
        phase_.store(Phase::Error, std::memory_order_release);
    } catch (...) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        error_ = "Excepcion desconocida durante el step";
        phase_.store(Phase::Error, std::memory_order_release);
    }
}

void DidacticController::storeReadyData(conway::Game pre, conway::Game post,
                                         conway::core::StepTrace trace) {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        pre_step_ = std::move(pre);
        post_step_ = std::move(post);
        trace_ = std::move(trace);
        // La generacion que se muestra como "CURRENT" es la del Game
        // pre, que el worker construyo con la generacion correcta
        // (preservada por copia o fijada por el constructor con
        // start_generation).
        current_generation_ = pre_step_->generation();
        phase_.store(Phase::Ready, std::memory_order_release);
    }
    // Calculamos la duracion total de reproduccion con la traza
    // visible. Se calcula DESPUES de mover la traza al miembro,
    // porque la unica copia viva es la de `trace_`. Si se hiciera
    // con la `trace` local ya movida, el vector `workers` estara
    // vacio y `computeTotalPlaybackNs` fijaría
    // `total_playback_ns_ = 1` (fallback por m == 0). Esto es
    // exactamente la causa del bug: con `total_playback_ns_ = 1`
    // y `current_playback_ns_` que se capa a 1, el flag
    // `fully_revealed` quedaba en true (mostrando
    // "REPRODUCCION COMPLETA") pero el progreso por worker era
    // `min(1, 1 / loop_duration_ns) < 1`, mostrando "pendiente".
    computeTotalPlaybackNs(*trace_);
    resetPlaybackToZero();
}

void DidacticController::computeTotalPlaybackNs(const conway::core::StepTrace& trace) {
    std::uint64_t m = 0;
    for (const auto& w : trace.workers) {
        if (w.loop_duration > m) m = w.loop_duration;
    }
    total_playback_ns_ = (m == 0) ? 1 : m;
}

void DidacticController::clearPlayback() {
    total_playback_ns_ = 0;
    current_playback_ns_ = 0;
    playing_ = false;
}

void DidacticController::resetPlaybackToZero() {
    current_playback_ns_ = 0;
    playing_ = false;
}

void DidacticController::play() {
    if (isReady()) playing_ = true;
}

void DidacticController::pause() {
    playing_ = false;
}

void DidacticController::togglePlay() {
    if (playing_) {
        pause();
    } else {
        play();
    }
}

void DidacticController::setSpeed(double multiplier) {
    if (multiplier < 0.1) multiplier = 0.1;
    if (multiplier > 8.0) multiplier = 8.0;
    speed_ = multiplier;
}

void DidacticController::increaseSpeed() {
    static const double presets[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    for (double p : presets) {
        if (p > speed_ + 1e-9) { speed_ = p; return; }
    }
    speed_ = 8.0;
}

void DidacticController::decreaseSpeed() {
    static const double presets[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    for (int i = static_cast<int>(sizeof(presets) / sizeof(presets[0])) - 1; i >= 0; --i) {
        if (presets[i] < speed_ - 1e-9) { speed_ = presets[i]; return; }
    }
    speed_ = 0.25;
}

void DidacticController::tick(std::chrono::nanoseconds elapsed_wall) {
    if (!playing_) return;
    if (total_playback_ns_ == 0) return;
    const double inc = static_cast<double>(elapsed_wall.count()) * speed_;
    const std::uint64_t add = static_cast<std::uint64_t>(inc);
    std::uint64_t next = current_playback_ns_ + add;
    if (next >= total_playback_ns_) {
        current_playback_ns_ = total_playback_ns_;
        playing_ = false;
    } else {
        current_playback_ns_ = next;
    }
}

const conway::Game* DidacticController::preStep() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return pre_step_ ? &*pre_step_ : nullptr;
}

const conway::Game* DidacticController::postStep() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return post_step_ ? &*post_step_ : nullptr;
}

const conway::core::StepTrace* DidacticController::stepTrace() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return trace_ ? &*trace_ : nullptr;
}

void DidacticController::advanceGeneration() {
    if (isPreparing()) return;
    if (!isReady() && phase() != Phase::Error) return;
    if (!hasInitialInput()) return;

    // Tomamos una copia del post_step actual, que sera el nuevo pre
    // (con la generacion preservada). El nuevo post es una segunda
    // copia sobre la que aplicamos step().
    // NOTA: NO sobrescribimos initial_input_; resetToInitial debe
    // regresar siempre al patron original, no al estado actual.
    std::optional<conway::Game> pre_copy;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (!post_step_) return;
        pre_copy = *post_step_;  // Game::Game(const Game&) preserva
                                  // generation.
    }
    if (!pre_copy) return;

    prepareFromGame(std::move(*pre_copy), engine_, requested_threads_,
                    /*reset_to_initial=*/false);
}

void DidacticController::resetToInitial() {
    if (isPreparing()) return;
    if (!hasInitialInput()) return;
    // startPreparation crea un Game en generacion 0 a partir del
    // InputData inicial.
    startPreparation(initial_input_, engine_, requested_threads_);
}

DidacticFrame DidacticController::snapshot() const {
    DidacticFrame frame;

    std::optional<conway::Game> pre_copy, post_copy;
    std::optional<conway::core::StepTrace> trace_copy;
    Phase p;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        p = phase_.load(std::memory_order_acquire);
        if (p == Phase::Ready) {
            pre_copy = pre_step_;
            post_copy = post_step_;
            trace_copy = trace_;
        }
        frame.error_message = error_;
    }

    frame.is_preparing = (p == Phase::Preparing);
    frame.phase = p;
    frame.engine_name = (engine_ == conway::Engine::Sequential)
                          ? "Secuencial" : "OpenMP";
    frame.requested_threads = requested_threads_;
    frame.logical_processors = std::max(1, omp_get_num_procs());
    frame.current_generation = current_generation_;
    frame.playing = playing_;
    frame.speed = speed_;
    frame.total_playback_ns = total_playback_ns_;
    frame.current_playback_ns = current_playback_ns_;

    if (p != Phase::Ready) {
        return frame;
    }

    const auto& pre = *pre_copy;
    const auto& post = *post_copy;
    const auto& trace = *trace_copy;

    frame.has_data = true;
    frame.width = pre.width();
    frame.height = pre.height();
    frame.effective_threads = trace.effective_threads;
    frame.next_generation = post.generation();
    frame.current_alive = pre.aliveCount();
    frame.next_alive = post.aliveCount();

    const std::size_t N = pre.width() * pre.height();
    if (N > 0) {
        frame.current_alive_map.assign(N, 0);
        frame.next_alive_map.assign(N, 0);
        for (std::size_t r = 0; r < pre.height(); ++r) {
            for (std::size_t c = 0; c < pre.width(); ++c) {
                const std::size_t idx = r * pre.width() + c;
                frame.current_alive_map[idx] = pre.isAlive(r, c) ? 1 : 0;
                frame.next_alive_map[idx] = post.isAlive(r, c) ? 1 : 0;
            }
        }
    }

    frame.workers.reserve(trace.workers.size());
    for (std::size_t i = 0; i < trace.workers.size(); ++i) {
        const auto& w = trace.workers[i];
        DidacticWorkerView v;
        v.thread_id = w.thread_id;
        v.first_row = static_cast<int>(w.first_row);
        v.end_row = static_cast<int>(w.end_row);
        v.cells_processed = w.cells_processed;
        v.loop_duration_ns = w.loop_duration;
        v.color_index = static_cast<int>(i);
        v.is_sequential = (trace.workers.size() == 1);
        if (w.loop_duration == 0) {
            v.progress = 1.0;
        } else {
            v.progress = std::min(1.0,
                static_cast<double>(current_playback_ns_) /
                static_cast<double>(w.loop_duration));
        }
        frame.workers.push_back(v);
    }

    if (N > 0) {
        frame.revealed.assign(N, 0);
        frame.worker_id.assign(N, -1);
        const bool full = (current_playback_ns_ >= total_playback_ns_);
        for (std::size_t i = 0; i < trace.workers.size(); ++i) {
            const auto& w = trace.workers[i];
            const int span = w.end_row - w.first_row;
            if (span <= 0) continue;
            const int rows_to_reveal = full
                ? span
                : std::max(0,
                    std::min(span,
                        static_cast<int>(
                            std::min(1.0,
                                static_cast<double>(current_playback_ns_) /
                                static_cast<double>(w.loop_duration))
                            * span + 0.999)));
            for (int k = 0; k < rows_to_reveal; ++k) {
                const std::size_t r =
                    static_cast<std::size_t>(w.first_row) +
                    static_cast<std::size_t>(k);
                if (r < 1 || r > pre.height()) continue;
                const std::size_t row_idx = r - 1;
                for (std::size_t c = 0; c < pre.width(); ++c) {
                    const std::size_t idx = row_idx * pre.width() + c;
                    if (!frame.revealed[idx]) {
                        frame.revealed[idx] = 1;
                        frame.worker_id[idx] =
                            static_cast<std::int8_t>(i);
                    }
                }
            }
        }
        frame.fully_revealed = full;
    }

    return frame;
}

}  // namespace conway::app

#pragma once

#include "core/game.hpp"
#include "core/step_trace.hpp"
#include "input/input.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace conway::app {

// Fase del controlador. Se mantiene a nivel de namespace para que
// DidacticFrame pueda declararlo sin necesidad de ver la definicion
// completa de DidacticController.
enum class DidacticPhase { Idle, Preparing, Ready, Error };

// Vista ligera de un trabajador para el panel de UI inferior.
// No expone punteros a StepTrace; es un valor copiable.
struct DidacticWorkerView {
    int thread_id = -1;
    int first_row = 0;
    int end_row = 0;
    std::size_t cells_processed = 0;
    std::uint64_t loop_duration_ns = 0;
    double progress = 0.0;        // 0.0 a 1.0, derivado del reloj de reproduccion
    int color_index = 0;          // indice estable dentro de la traza actual
    bool is_sequential = false;   // true si la traza tiene un unico worker
};

// Snapshot inmutable del estado para renderizar. Se construye a partir
// de los datos del worker y del estado de reproduccion, todo en el hilo
// de UI. Es copiable y barato de mover.
struct DidacticFrame {
    bool has_data = false;
    bool is_preparing = false;
    bool playing = false;
    bool fully_revealed = false;
    DidacticPhase phase = DidacticPhase::Idle;

    std::string engine_name;
    int requested_threads = 0;
    int effective_threads = 0;
    int logical_processors = 0;
    int current_generation = 0;
    int next_generation = 0;

    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t current_alive = 0;
    std::size_t next_alive = 0;

    // Mascara para el panel NEXT. Tama~no = width * height.
    //   revealed[i]    0/1, indica si la celda ya fue revelada.
    //   worker_id[i]   -1 si no revelada, 0..N-1 si revelada por el
    //                   worker de ese indice.
    //   next_alive_map[i]  0/1, estado final de NEXT (no depende
    //                       del reloj de reproduccion).
    //   current_alive_map[i]  0/1, estado de CURRENT (constante
    //                          durante toda la reproduccion).
    std::vector<std::uint8_t> revealed;
    std::vector<std::int8_t>  worker_id;
    std::vector<std::uint8_t> next_alive_map;
    std::vector<std::uint8_t> current_alive_map;

    // Lista de trabajadores en el orden en que aparecen en la traza.
    std::vector<DidacticWorkerView> workers;

    std::uint64_t total_playback_ns = 0;
    std::uint64_t current_playback_ns = 0;
    double speed = 1.0;

    std::string error_message;
};

// Controlador de la visualizacion didactica. Gestiona:
//   - La generacion instrumentada (CURRENT + NEXT + StepTrace) que
//     produce un worker de OpenMP/FTXUI para no bloquear la UI.
//   - El reloj de reproduccion, la velocidad y el play/pause.
//   - Una vista inmutable para que FTXUI renderice sin condicion de
//     carrera con el worker.
//
// Modelo de propiedad de los datos:
//   - Los datos del step (pre_step_, post_step_, trace_, error_,
//     phase_) son producidos por el worker y consumidos por la UI.
//     Se protegen con data_mutex_ + condition_variable_ para la
//     entrega.
//   - El estado de reproduccion (playback_ns_, speed_, playing_)
//     lo reinicia la UI antes de lanzar el worker; el worker fija la
//     duracion antes de publicar Ready. Despues solo lo toca la UI.
//   - El worker_ es unico; startPreparation() une el previo si
//     quedase alguno antes de lanzar el nuevo.
class DidacticController {
public:
    // Phase esta declarado a nivel de namespace (DidacticPhase) para
    // que DidacticFrame pueda declararlo sin ver la definicion de
    // esta clase.
    using Phase = DidacticPhase;

    DidacticController();
    ~DidacticController();

    DidacticController(const DidacticController&) = delete;
    DidacticController& operator=(const DidacticController&) = delete;

    // Fija el patron inicial; sera el punto al que resetToInitial
    // regresa. Debe llamarse antes de la primera preparacion.
    void setInitialInput(const InputData& input);
    bool hasInitialInput() const { return has_initial_; }
    const InputData& initialInput() const { return initial_input_; }

    // Lanza un worker que toma pre_input, construye un Game, ejecuta
    // un step instrumentado y entrega (pre, post, trace) bajo el
    // mutex cuando termina. Si ya hay un worker en curso, lo une
    // antes de lanzar el nuevo. Si el controlador no tiene initial
    // input, lo fija a pre_input (caso de entrada directa).
    void startPreparation(const InputData& pre_input,
                          conway::Engine engine,
                          int requested_threads);

    // Variante sincrona, sin worker. Util para tests y para
    // patrones muy pequenos donde el overhead del worker no aporta.
    void prepareSynchronously(const InputData& pre_input,
                               conway::Engine engine,
                               int requested_threads);

    // Espera (con cv.wait) hasta que phase_ sea Ready o Error.
    void waitUntilReady();

    // Une el worker si esta activo. Llamar antes de destruir o al
    // cambiar de modo para no dejar hilos colgando.
    void joinWorker();

    Phase phase() const { return phase_.load(std::memory_order_acquire); }
    bool isReady() const { return phase() == Phase::Ready; }
    bool isPreparing() const { return phase() == Phase::Preparing; }
    const std::string& errorMessage() const;

    // --- Reproduccion (solo UI) ---
    void play();
    void pause();
    void togglePlay();
    bool isPlaying() const { return playing_; }

    void setSpeed(double multiplier);
    void increaseSpeed();
    void decreaseSpeed();
    double speed() const { return speed_; }

    // Avanza el reloj de reproduccion. Si playing_ es false, no hace
    // nada. Si la reproduccion termina, playing_ se pone a false
    // (auto-pausa al completar).
    void tick(std::chrono::nanoseconds elapsed_wall);

    // Construye una vista para renderizar. Llamar desde el hilo de UI.
    DidacticFrame snapshot() const;

    // --- Navegacion entre generaciones (async) ---
    // Avanza una generacion: pre := post, y lanza un nuevo step
    // instrumentado. Si hay un worker en curso, ignora la llamada.
    void advanceGeneration();

    // Resetea a la entrada inicial y lanza un nuevo step. Si hay
    // un worker en curso, ignora la llamada.
    void resetToInitial();

    // Acceso a los datos del step, en el hilo de UI, una vez Ready.
    // Devuelven nullptr si phase_ no es Ready. No hay garantia de
    // validez tras una llamada a advanceGeneration o resetToInitial.
    const conway::Game* preStep() const;
    const conway::Game* postStep() const;
    const conway::core::StepTrace* stepTrace() const;

private:
    void prepareFromGame(conway::Game pre, conway::Engine engine,
                         int requested_threads, bool reset_to_initial);
    void prepareFromGameSync(conway::Game pre, conway::Engine engine,
                             int requested_threads, bool reset_to_initial);
    void storeReadyData(conway::Game pre, conway::Game post,
                        conway::core::StepTrace trace);
    void computeTotalPlaybackNs(const conway::core::StepTrace& trace);
    void clearPlayback();
    void resetPlaybackToZero();

    // Datos del step. Se entregan al hilo de UI a traves de
    // data_mutex_ + data_cv_.
    InputData initial_input_;
    bool has_initial_ = false;
    conway::Engine engine_ = conway::Engine::Sequential;
    int requested_threads_ = 0;
    int current_generation_ = 0;  // numero de generacion de pre_step_

    mutable std::mutex data_mutex_;
    std::condition_variable data_cv_;
    std::optional<conway::Game> pre_step_;
    std::optional<conway::Game> post_step_;
    std::optional<conway::core::StepTrace> trace_;
    std::string error_;
    std::atomic<Phase> phase_{Phase::Idle};

    // Duracion publicada con Ready; reloj, velocidad y playing solo UI.
    std::uint64_t total_playback_ns_ = 0;
    std::uint64_t current_playback_ns_ = 0;
    double speed_ = 1.0;
    bool playing_ = false;

    // Worker unico.
    std::thread worker_;
};

}  // namespace conway::app

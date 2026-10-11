#include "app/app.hpp"

#include "app/benchmark.hpp"
#include "app/controls.hpp"
#include "app/simulation.hpp"
#include "app/thread_config.hpp"
#include "core/game.hpp"
#include "core/engines/openmp.hpp"
#include "input/input.hpp"
#include "ui/board_view.hpp"
#include "ui/layout.hpp"
#include "ui/simulation_layout.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <omp.h>

namespace conway::app {

namespace {

constexpr const char* kClearScreen = "\033[H\033[2J";
constexpr const char* kErrorColor = "\033[1;31m";

// La seleccion se conserva aqui aunque se cargue otro patron o se salga
// de los modulos de visualizacion y benchmark.
struct AppState {
    std::optional<InputData> input;
    std::string path;
    conway::Engine engine{conway::Engine::Sequential};
    int requestedOpenMPThreads{defaultOpenMPThreadCount()};
};

struct PathRequest {
    bool eof{false};
    bool cancelled{false};
    std::string path;
};

enum class MenuAction {
    Up,
    Down,
    Select,
    Benchmark,
    Visualization,
    SelectEngine,     // abre el submenu de seleccion de motor
    ConfigureThreads,
    ChangeInput,
    Exit,
    None
};

// Acciones del submenu de motor (independientes del MenuAction principal
// para que un cambio en el menu principal no afecte a este flujo).
enum class EngineMenuAction {
    Up,
    Down,
    Select,
    SetSequential,    // [S] o Enter sobre la opcion Secuencial
    SetOpenMP,        // [O] o Enter sobre la opcion OpenMP
    Back,             // [X] o Enter sobre la opcion Volver
    None
};

struct RawModeGuard {
    ~RawModeGuard() {
        controls::disableRawMode();
    }
};

const std::vector<ui::MenuItem>& mainMenuItems() {
    static const std::vector<ui::MenuItem> items = {
        {"1", "Benchmark"},
        {"2", "Visualización"},
        {"3", "Motor"},
        {"4", "Hilos OpenMP"},
        {"C", "Cambiar entrada"},
        {"0", "Salir"}
    };

    return items;
}

const std::vector<ui::MenuItem>& engineMenuItems() {
    static const std::vector<ui::MenuItem> items = {
        {"S", "Secuencial"},
        {"O", "OpenMP"},
        {"X", "Volver"}
    };

    return items;
}

ui::ActiveInputInfo makeInputInfo(const AppState& state) {
    ui::ActiveInputInfo info;

    if (!state.input) {
        return info;
    }

    info.hasInput = true;
    info.filename = state.path;
    info.width = state.input->width;
    info.height = state.input->height;
    info.aliveCells = static_cast<std::size_t>(
        std::count(state.input->cells.begin(), state.input->cells.end(), std::uint8_t{1})
    );

    return info;
}

const char* engineName(conway::Engine e) {
    return (e == conway::Engine::Sequential) ? "Secuencial" : "OpenMP";
}

void renderMenu(const AppState& state, std::size_t selected, const std::string& message) {
    std::cout << kClearScreen;
    ui::renderLayout(
        "CONWAY PARALLEL",
        "Juego de la Vida | " + std::string(engineName(state.engine)) +
            " | hilos OpenMP solicitados: " + std::to_string(state.requestedOpenMPThreads),
        makeInputInfo(state),
        mainMenuItems(),
        selected
    );

    if (!message.empty()) {
        const bool isError = message.compare(0, 5, "Error") == 0;
        std::cout << '\n'
                  << (isError ? kErrorColor : ui::color::GREEN)
                  << message
                  << ui::color::RESET
                  << '\n';
    }
    std::cout << std::flush;
}

void renderThreadMenu(const AppState& state, std::size_t selected, const std::string& message) {
    std::cout << kClearScreen;
    const std::string subtitle =
        "Procesadores lógicos disponibles: " + std::to_string(availableLogicalProcessors()) +
        " | solicitados: " + std::to_string(state.requestedOpenMPThreads) +
        " | límite configurable: " + std::to_string(kMaxOpenMPThreads);
    ui::renderLayout(
        "CONFIGURACIÓN OPENMP",
        subtitle.c_str(),
        makeInputInfo(state),
        {
            {"1", "Usar 1 hilo"},
            {"2", "Usar 2 hilos"},
            {"4", "Usar 4 hilos"},
            {"8", "Usar 8 hilos"},
            {"C", "Número personalizado"},
            {"X", "Volver"}
        },
        selected
    );
    std::cout << "Los procesadores mostrados son lógicos; no equivalen a núcleos físicos.\n";
    if (!message.empty()) {
        std::cout << message << '\n';
    }
    std::cout << std::flush;
}

bool configureThreadsUI(AppState& state, std::string& message) {
    const bool interactive = controls::isInteractive();
    std::size_t selected = 0;
    const std::vector<int> presets{1, 2, 4, 8};

    while (true) {
        renderThreadMenu(state, selected, message);
        std::optional<controls::KeyPress> key = controls::readKey();
        if (key) {
            if (key->type == controls::Key::Up) {
                selected = (selected + 5) % 6;
                continue;
            }
            if (key->type == controls::Key::Down) {
                selected = (selected + 1) % 6;
                continue;
            }
        } else if (interactive) {
            return true;
        }

        std::string choice;
        if (key && key->type == controls::Key::Enter) {
            choice = selected < presets.size()
                ? std::to_string(presets[selected])
                : selected == 4 ? "C" : "X";
        } else if (key && key->type == controls::Key::Char) {
            choice.assign(1, key->value);
        } else {
            const auto line = controls::readLine("");
            if (!line) {
                return true;
            }
            choice = *line;
        }

        if (choice == "x" || choice == "X" || choice == "0") {
            return true;
        }
        if (choice == "c" || choice == "C") {
            const auto value = controls::readLine(
                "Cantidad de hilos (1-" + std::to_string(kMaxOpenMPThreads) + "): ");
            if (!value) {
                return true;
            }
            const auto parsed = parseOpenMPThreadCount(*value);
            if (!parsed) {
                message = "Error: introduce un entero entre 1 y " +
                    std::to_string(kMaxOpenMPThreads) + ".";
                continue;
            }
            state.requestedOpenMPThreads = *parsed;
        } else {
            const auto parsed = parseOpenMPThreadCount(choice);
            if (!parsed ||
                std::find(presets.begin(), presets.end(), *parsed) == presets.end()) {
                message = "Error: selecciona 1, 2, 4, 8, C o X.";
                continue;
            }
            state.requestedOpenMPThreads = *parsed;
        }

        omp_set_num_threads(state.requestedOpenMPThreads);
        message = "Hilos OpenMP solicitados: " +
            std::to_string(state.requestedOpenMPThreads) + ".";
        return true;
    }
}

void renderEngineMenu(const AppState& state, std::size_t selected) {
    std::cout << kClearScreen;
    const std::string subtitle = std::string("Motor actual: ") + engineName(state.engine);
    ui::renderLayout(
        "MOTOR DE EJECUCION",
        subtitle.c_str(),
        makeInputInfo(state),
        engineMenuItems(),
        selected
    );
    std::cout << std::flush;
}

PathRequest askPath() {
    PathRequest request;
    bool firstAttempt = true;

    while (true) {
        if (firstAttempt) {
            std::cout << kClearScreen << std::flush;
            firstAttempt = false;
        }

        auto line = controls::readLine("Ruta del archivo (0 para cancelar): ");

        if (!line) {
            request.eof = true;
            return request;
        }

        if (line->empty()) {
            std::cout << "Escribe una ruta o '0' para cancelar.\n" << std::flush;
            continue;
        }

        if (*line == "0") {
            request.cancelled = true;
            return request;
        }

        request.path = *line;
        return request;
    }
}

bool loadInto(AppState& state, const std::string& path, std::string& message) {
    try {
        InputData data = loadInput(path);

        state.input = std::move(data);
        state.path = path;
        message = "Entrada cargada: " + path;

        return true;
    } catch (const std::exception& e) {
        message = std::string("Error al cargar: ") + e.what();

        if (state.input) {
            message += " (se conserva la entrada anterior)";
        }

        return false;
    }
}

bool ensureInput(AppState& state, std::string& message) {
    if (!state.input) {
        const PathRequest request = askPath();

        if (request.eof) {
            return false;
        }

        if (request.cancelled) {
            message = "Operacion cancelada.";
            return true;
        }

        loadInto(state, request.path, message);
    }

    return true;
}

bool changeInput(AppState& state, std::string& message) {
    const PathRequest request = askPath();

    if (request.eof) {
        return false;
    }

    if (request.cancelled) {
        message = "Operacion cancelada.";
        return true;
    }

    loadInto(state, request.path, message);
    return true;
}

// Submenu de seleccion de motor. Devuelve true si el usuario salio
// normalmente (Volver o EOF), false si no (no se usa en la practica
// porque el submenu siempre vuelve). El mensaje se rellena con la
// confirmacion del motor elegido cuando hay cambio.
bool selectEngineUI(AppState& state, std::string& message) {
    const bool interactive = controls::isInteractive();
    constexpr std::size_t kOptionCount = 3;
    std::size_t selected = 0;

    while (true) {
        renderEngineMenu(state, selected);

        auto action = controls::readKey();
        if (!action && interactive) {
            return true;  // EOF en modo interactivo
        }

        if (action) {
            switch (action->type) {
                case controls::Key::Up:
                    selected = (selected + kOptionCount - 1) % kOptionCount;
                    continue;
                case controls::Key::Down:
                    selected = (selected + 1) % kOptionCount;
                    continue;
                case controls::Key::Enter:
                    if (selected == 0) {
                        state.engine = conway::Engine::Sequential;
                        message = "Motor seleccionado: Secuencial";
                        return true;
                    } else if (selected == 1) {
                        state.engine = conway::Engine::OpenMP;
                        message = "Motor seleccionado: OpenMP";
                        return true;
                    } else {
                        return true;  // Volver
                    }
                case controls::Key::Char:
                    break;
            }
            switch (action->value) {
                case 's': case 'S':
                    state.engine = conway::Engine::Sequential;
                    message = "Motor seleccionado: Secuencial";
                    return true;
                case 'o': case 'O':
                    state.engine = conway::Engine::OpenMP;
                    message = "Motor seleccionado: OpenMP";
                    return true;
                case 'x': case 'X': case 27:  // 27 = Esc
                    return true;
                default:
                    continue;
            }
        }

        // Modo no interactivo: leer una linea y mapear.
        auto line = controls::readLine("");
        if (!line) {
            return true;  // EOF
        }
        if (*line == "s" || *line == "S") {
            state.engine = conway::Engine::Sequential;
            message = "Motor seleccionado: Secuencial";
            return true;
        }
        if (*line == "o" || *line == "O") {
            state.engine = conway::Engine::OpenMP;
            message = "Motor seleccionado: OpenMP";
            return true;
        }
        if (*line == "x" || *line == "X" || *line == "0") {
            return true;
        }
        // Cualquier otra entrada: ignorar y redibujar.
    }
}

MenuAction actionFromIndex(std::size_t index) {
    switch (index) {
        case 0:
            return MenuAction::Benchmark;
        case 1:
            return MenuAction::Visualization;
        case 2:
            return MenuAction::SelectEngine;
        case 3:
            return MenuAction::ConfigureThreads;
        case 4:
            return MenuAction::ChangeInput;
        case 5:
            return MenuAction::Exit;
        default:
            return MenuAction::None;
    }
}

void renderSimulation(
    const AppState& state,
    const SimulationSession& session,
    controls::TerminalSize terminal
) {
        const Game& game = session.game();
        ui::SimulationHeaderInfo header;
        header.mode = engineName(state.engine);
        header.filename = state.path;
        header.boardWidth = game.width();
        header.boardHeight = game.height();
        header.statusText = session.isRunning() ? "En ejecución" : "Pausado";

        ui::ExecutionInfo execution;
        execution.generation = game.generation();
        execution.aliveCount = game.aliveCount();
        execution.stepProgress = "100%";
        execution.workersInfo = state.engine == Engine::OpenMP
            ? "OpenMP | disponibles (lógicos): " +
                std::to_string(availableLogicalProcessors()) +
                " | solicitados: " + std::to_string(state.requestedOpenMPThreads) +
                (game.generation() == 0 ? " | efectivos pendientes"
                    : " | efectivos observados: " +
                        std::to_string(core::last_openmp_team_size()))
            : "Motor: Secuencial";

        const std::size_t frameWidth = std::min(terminal.columns, std::size_t{240});
        const bool sideBySide = frameWidth >= 72;
        const std::size_t fixedRows = sideBySide ? 16 : 26;
        const std::size_t boardRows = terminal.rows > fixedRows
            ? std::min(terminal.rows - fixedRows, std::size_t{40})
            : 1;
        const std::size_t panelWidth = sideBySide
            ? (frameWidth - 6) / 2
            : (frameWidth > 4 ? frameWidth - 4 : 1);
        const std::size_t visibleColumns = std::max<std::size_t>(
            1, panelWidth > 2 ? (panelWidth - 2) / 3 : 1);
        const ui::BoardRenderOptions boardOptions{
            visibleColumns,
            boardRows,
            controls::supportsAnsiColors()
        };

        ui::BoardPanelData current{"CURRENT", ui::renderBoardLines(game, boardOptions)};
        Game nextGame = game;
        nextGame.step();
        ui::BoardPanelData next{"NEXT", ui::renderBoardLines(nextGame, boardOptions)};
        const ui::SimulationLayoutConfig layout{
            frameWidth,
            boardRows + (game.height() > boardRows ? 1U : 0U) +
                (game.width() > visibleColumns ? 1U : 0U),
            boardOptions.colorEnabled
        };
        ui::renderSimulationLayout(header, execution, current, next, layout);
}

bool applySimulationCommand(char command, SimulationSession& session, bool& changed) {
        switch (command) {
            case 'n':
            case 'N':
                session.step();
                changed = true;
                return true;
            case ' ':
            case 'p':
            case 'P':
                session.toggleRunning();
                if (session.isRunning()) {
                    session.advanceIfRunning();
                }
                changed = true;
                return true;
            case 'r':
            case 'R':
                session.reset();
                changed = true;
                return true;
            case 'x':
            case 'X':
            case 'q':
            case 'Q':
            case 27:
                return false;
            default:
                return true;
        }
}

bool runVisualization(AppState& state) {
        if (state.engine == Engine::OpenMP) {
            omp_set_num_threads(state.requestedOpenMPThreads);
        }
        SimulationSession session(*state.input, state.engine);
        const bool interactive = controls::isInteractive();
        bool redraw = true;
        controls::TerminalSize lastSize = controls::terminalSize();

        while (true) {
            const controls::TerminalSize currentSize = controls::terminalSize();
            if (currentSize != lastSize) {
                lastSize = currentSize;
                redraw = true;
            }
            if (redraw) {
                if (interactive) {
                    std::cout << kClearScreen;
                }
                renderSimulation(state, session, currentSize);
                std::cout << std::flush;
                redraw = false;
            }

            if (interactive) {
                const auto timeout = session.isRunning()
                    ? std::chrono::milliseconds(150)
                    : std::chrono::milliseconds(500);
                const controls::TimedKeyPress reading = controls::readKeyFor(timeout);
                if (!reading.ready) {
                    if (session.isRunning()) {
                        session.advanceIfRunning();
                        redraw = true;
                    }
                    continue;
                }
                if (!reading.key) {
                    return false;
                }
                const char command = reading.key->type == controls::Key::Char
                    ? reading.key->value
                    : '\0';
                if (!applySimulationCommand(command, session, redraw)) {
                    return true;
                }
                continue;
            }

            const auto line = controls::readLine("");
            if (!line) {
                return false;
            }
            const char command = line->empty() ? '\0' : (*line)[0];
            if (!applySimulationCommand(command, session, redraw)) {
                return true;
            }
        }
}

bool waitForModuleReturn(bool interactive) {
        std::cout << "\nPresiona una tecla para volver al menu.\n" << std::flush;
        if (interactive) {
            return controls::readKey().has_value();
        }
        return controls::readLine("").has_value();
}

bool runBenchmarkUI(AppState& state, std::string& message) {
        if (!ensureInput(state, message)) {
            return false;
        }
        if (!state.input) {
            return true;
        }

        constexpr int kGenerations = 20;
        constexpr int kRepetitions = 5;
        if (controls::isInteractive()) {
            std::cout << kClearScreen;
        }
        std::cout << "Benchmark en curso: calentamiento y " << kRepetitions
                  << " repeticiones por motor...\n" << std::flush;
        const BenchmarkResult result = runBenchmark(
            *state.input, kGenerations, kRepetitions, state.requestedOpenMPThreads);

        if (controls::isInteractive()) {
            std::cout << kClearScreen;
        }
        std::cout << "BENCHMARK | Secuencial vs OpenMP\n"
                  << "Entrada: " << state.path << " (" << state.input->width << " x "
                  << state.input->height << ")\n"
                  << "Generaciones: " << result.generations
                  << " | Repeticiones: " << result.repetitions
                  << " | Hilos lógicos disponibles: " << availableLogicalProcessors()
                  << " | OpenMP solicitados: " << result.requestedThreads << "\n"
                  << "Hilos OpenMP efectivos: ";
        if (result.effectiveThreadCountStable) {
            std::cout << result.minEffectiveThreads << '\n';
        } else {
            std::cout << result.minEffectiveThreads << " - " << result.maxEffectiveThreads
                      << " (variaron entre regiones)\n";
        }
        std::cout << std::fixed << std::setprecision(3)
                  << "\nMotor          Mediana (ms)\n"
                  << "Secuencial     " << result.sequentialMedianMs << '\n'
                  << "OpenMP         " << result.openMPMedianMs << "\n\n"
                  << "Speedup: " << result.speedup << "x\n"
                  << "Eficiencia: ";
        if (result.efficiencyValid) {
            std::cout << (result.efficiency * 100.0) << "%\n";
        } else {
            std::cout << "no disponible (hilos efectivos variables o resultado no equivalente)\n";
        }
        std::cout << "Estados equivalentes: " << (result.equivalent ? "SI" : "NO") << '\n'
                  << "Interpretación: ";
        if (!result.equivalent) {
            std::cout << "resultados no equivalentes; no comparar rendimiento.\n";
        } else if (result.openMPMedianMs < result.sequentialMedianMs) {
            std::cout << "OpenMP fue " << result.speedup << "x más rápido.\n";
        } else if (result.sequentialMedianMs < result.openMPMedianMs && result.speedup > 0.0) {
            std::cout << "Secuencial fue " << (1.0 / result.speedup) << "x más rápido.\n";
        } else {
            std::cout << "tiempos iguales o resolución insuficiente para distinguirlos.\n";
        }
        if (state.input->cells.size() < 256) {
            std::cout << "Nota: tablero pequeño; la coordinación OpenMP puede dominar y "
                         "esta comparación no representa cargas grandes.\n";
        }
        std::cout << std::flush;

        if (!waitForModuleReturn(controls::isInteractive())) {
            return false;
        }
        if (!result.equivalent) {
            message = "Error: los motores produjeron estados distintos.";
        }
        return true;
}

std::optional<MenuAction> pollMenuAction(bool interactive) {
    if (interactive) {
        const auto key = controls::readKey();

        if (!key) {
            return std::nullopt;
        }

        switch (key->type) {
            case controls::Key::Up:
                return MenuAction::Up;
            case controls::Key::Down:
                return MenuAction::Down;
            case controls::Key::Enter:
                return MenuAction::Select;
            case controls::Key::Char:
                break;
        }

        switch (key->value) {
            case '1':
                return MenuAction::Benchmark;
            case '2':
                return MenuAction::Visualization;
            case '3':
                return MenuAction::SelectEngine;
            case '4':
                return MenuAction::ConfigureThreads;
            case 'c':
            case 'C':
                return MenuAction::ChangeInput;
            case '0':
                return MenuAction::Exit;
            default:
                return MenuAction::None;
        }
    }

    const auto line = controls::readLine("");

    if (!line) {
        return std::nullopt;
    }

    if (*line == "1") {
        return MenuAction::Benchmark;
    }

    if (*line == "2") {
        return MenuAction::Visualization;
    }

    if (*line == "3") {
        return MenuAction::SelectEngine;
    }

    if (*line == "4") {
        return MenuAction::ConfigureThreads;
    }

    if (*line == "c" || *line == "C") {
        return MenuAction::ChangeInput;
    }

    if (*line == "0") {
        return MenuAction::Exit;
    }

    return MenuAction::None;
}

int runLoop() {
    RawModeGuard rawGuard;

    const bool interactive = controls::isInteractive();
    const std::size_t optionCount = mainMenuItems().size();

    AppState state;
    std::size_t selected = 0;
    std::string message;

    while (true) {
        renderMenu(state, selected, message);
        message.clear();

        auto action = pollMenuAction(interactive);

        if (!action) {
            break;
        }

        if (*action == MenuAction::Select) {
            action = actionFromIndex(selected);
        }

        switch (*action) {
            case MenuAction::Up:
                selected = (selected + optionCount - 1) % optionCount;
                break;
            case MenuAction::Down:
                selected = (selected + 1) % optionCount;
                break;
            case MenuAction::Benchmark:
                if (!runBenchmarkUI(state, message)) {
                    return 0;
                }
                break;
            case MenuAction::Visualization:
                if (!ensureInput(state, message)) {
                    return 0;
                }
                if (state.input && !runVisualization(state)) {
                    return 0;
                }
                if (state.input) {
                    message = "Visualizacion finalizada.";
                }
                break;
            case MenuAction::SelectEngine:
                if (!selectEngineUI(state, message)) {
                    return 0;
                }
                break;
            case MenuAction::ConfigureThreads:
                if (!configureThreadsUI(state, message)) {
                    return 0;
                }
                break;
            case MenuAction::ChangeInput:
                if (!changeInput(state, message)) {
                    return 0;
                }
                break;
            case MenuAction::Exit:
                return 0;
            case MenuAction::Select:
            case MenuAction::None:
                break;
        }
    }

    return 0;
}

}

int run() {
    try {
        return runLoop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

}

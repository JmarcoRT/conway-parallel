#include "app/app.hpp"

#include "app/benchmark.hpp"
#include "app/controls.hpp"
#include "app/ftxui_app.hpp"
#include "app/pattern_catalog.hpp"
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
#include <clocale>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <omp.h>
#include <poll.h>
#include <unistd.h>

namespace conway::app {

namespace {

constexpr const char* kClearScreen = "\033[H\033[2J";

// La seleccion se conserva aqui aunque se cargue otro patron o se salga
// de los modulos de visualizacion y benchmark.
struct AppState {
    std::optional<InputData> input;
    std::string path;
    conway::Engine engine{conway::Engine::Sequential};
    int requestedOpenMPThreads{defaultOpenMPThreadCount()};
    int mpiProcesses{1};
    std::unique_ptr<SimulationSession> simulation;
    bool replaceSimulation{false};
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
    RawModeGuard() {
        controls::beginTerminalSession();
    }

    ~RawModeGuard() {
        controls::endTerminalSession();
    }
};

ui::LayoutConfig currentLayoutConfig() {
    const auto size = controls::terminalSize();
    return {size.columns, size.rows, controls::supportsAnsiColors()};
}

void clearScreen() {
    if (controls::isInteractive()) {
        std::cout << kClearScreen;
    }
}

std::optional<controls::KeyPress> readKeyWithResize(
    controls::TerminalSize& previousSize,
    bool& resized
) {
    resized = false;
    while (true) {
        const auto reading = controls::readKeyFor(std::chrono::milliseconds(250));
        if (reading.ready) {
            return reading.key;
        }
        const auto currentSize = controls::terminalSize();
        if (currentSize != previousSize) {
            previousSize = currentSize;
            resized = true;
            return std::nullopt;
        }
    }
}

const std::vector<ui::MenuItem>& mainMenuItems() {
    static const std::vector<ui::MenuItem> items = {
        {"1", "Benchmark"},
        {"2", "Visualización"},
        {"3", "Motor"},
        {"4", "Hilos OpenMP"},
        {"C", "Catálogo de patrones"},
        {"0", "Salir"}
    };

    return items;
}

const std::vector<ui::MenuItem>& engineMenuItems() {
    static const std::vector<ui::MenuItem> items = {
        {"S", "Secuencial"},
        {"O", "OpenMP"},
        {"M", "MPI"},
        {"P", "Configurar procesos MPI"},
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
    return e == conway::Engine::Sequential ? "Secuencial" : e == conway::Engine::OpenMP ? "OpenMP" : "MPI";
}

void renderMenu(const AppState& state, std::size_t selected, const std::string& message) {
    clearScreen();
    const std::string subtitle =
        "Juego de la Vida  |  " + std::string(engineName(state.engine)) +
        "  |  OpenMP solicitado: " + std::to_string(state.requestedOpenMPThreads);
    ui::renderLayout(
        "CONWAY PARALLEL",
        subtitle,
        makeInputInfo(state),
        mainMenuItems(),
        selected,
        currentLayoutConfig(),
        message
    );
    std::cout << std::flush;
}

void renderThreadMenu(const AppState& state, std::size_t selected, const std::string& message) {
    clearScreen();
    const std::string subtitle =
        "Procesadores lógicos: " + std::to_string(availableLogicalProcessors()) +
        " | solicitados: " + std::to_string(state.requestedOpenMPThreads) +
        " | límite 1-" + std::to_string(kMaxOpenMPThreads) +
        " | no equivale a núcleos físicos";
    ui::renderLayout(
        "CONFIGURACIÓN OPENMP",
        subtitle,
        makeInputInfo(state),
        {
            {"1", "Usar 1 hilo"},
            {"2", "Usar 2 hilos"},
            {"4", "Usar 4 hilos"},
            {"8", "Usar 8 hilos"},
            {"C", "Número personalizado"},
            {"X", "Volver"}
        },
        selected,
        currentLayoutConfig(),
        message
    );
    std::cout << std::flush;
}

bool configureThreadsUI(AppState& state, std::string& message) {
    const bool interactive = controls::isInteractive();
    std::size_t selected = 0;
    const std::vector<int> presets{1, 2, 4, 8};
    auto lastSize = controls::terminalSize();

    while (true) {
        renderThreadMenu(state, selected, message);
        bool resized = false;
        std::optional<controls::KeyPress> key;
        if (interactive) {
            key = readKeyWithResize(lastSize, resized);
            if (resized) {
                continue;
            }
            if (!key) {
                return true;
            }
        }
        if (key) {
            if (key->type == controls::Key::Up) {
                selected = (selected + 5) % 6;
                continue;
            }
            if (key->type == controls::Key::Down) {
                selected = (selected + 1) % 6;
                continue;
            }
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

        if (choice == "x" || choice == "X" || choice == "0" || choice == "\x1b") {
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
    clearScreen();
    const std::string subtitle = std::string("Motor actual: ") + engineName(state.engine);
    ui::renderLayout(
        "MOTOR DE EJECUCION",
        subtitle.c_str(),
        makeInputInfo(state),
        engineMenuItems(),
        selected,
        currentLayoutConfig()
    );
    std::cout << std::flush;
}

PathRequest askPath() {
    PathRequest request;
    bool firstAttempt = true;

    while (true) {
        if (firstAttempt) {
            clearScreen();
            std::cout << std::flush;
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
        if (state.simulation && !state.simulation->closed()) state.simulation->close();
        state.replaceSimulation = true;
        state.input = std::move(data);
        state.path = path;
        message = "Entrada cargada: " + path;

        return true;
    } catch (const std::exception& e) {
        message = "Error al cargar";
        if (state.input) {
            message += " (se conserva la entrada anterior)";
        }
        message += ": " + std::string(e.what());

        return false;
    }
}

bool selectPatternFromCatalog(AppState& state, std::string& message) {
    const ui::ActiveInputInfo active = makeInputInfo(state);
    const PatternCatalog catalog = discoverPatterns("patterns");
    if (!catalog.error.empty()) {
        message = "Error: " + catalog.error;
        return true;
    }

    std::vector<ui::MenuItem> items;
    items.reserve(catalog.entries.size() + 2);
    std::size_t selected = 0;
    for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
        const PatternEntry& entry = catalog.entries[index];
        std::string label = entry.name;
        if (entry.valid()) {
            label += "  ·  " + std::to_string(entry.width) + " × " +
                std::to_string(entry.height);
            if (!entry.description.empty()) {
                label += "  ·  " + entry.description;
            }
            if (entry.path == state.path) {
                selected = index;
            }
        } else {
            label += "  ·  inválido: " + entry.error;
        }
        items.push_back({std::to_string((index + 1) % 10), std::move(label)});
    }
    const std::size_t manualIndex = items.size();
    items.push_back({"M", "Introducir una ruta manual"});
    const std::size_t backIndex = items.size();
    items.push_back({"X", "Volver sin cambiar"});
    if (catalog.entries.empty()) {
        selected = manualIndex;
    }

    const std::string subtitle =
        "Catálogo dinámico: patterns/*.txt  |  Añade archivos .txt para descubrirlos" +
        (active.hasInput ? "  |  Actual: " + active.filename : "  |  Sin patrón seleccionado");
    const bool interactive = controls::isInteractive();
    auto lastSize = controls::terminalSize();
    while (true) {
        clearScreen();
        ui::renderLayout("SELECCIONAR PATRÓN", subtitle, active, items, selected,
                         currentLayoutConfig(), message);
        std::cout << std::flush;

        if (!interactive) {
            const auto choice = controls::readLine("");
            if (!choice) {
                return false;
            }
            if (*choice == "x" || *choice == "X" || *choice == "0") {
                message = "Selección de patrón cancelada.";
                return true;
            }
            if (*choice == "m" || *choice == "M") {
                const PathRequest request = askPath();
                if (request.eof) {
                    return false;
                }
                if (!request.cancelled) {
                    loadInto(state, request.path, message);
                }
                return true;
            }
            std::size_t index = catalog.entries.size();
            try {
                const auto parsed = static_cast<std::size_t>(std::stoul(*choice));
                if (parsed > 0 && parsed <= catalog.entries.size()) {
                    index = parsed - 1;
                }
            } catch (const std::exception&) {
                index = catalog.entries.size();
            }
            if (index < catalog.entries.size()) {
                if (!catalog.entries[index].valid()) {
                    message = "Error al cargar: " + catalog.entries[index].error +
                        " (se conserva la entrada anterior)";
                    continue;
                }
                loadInto(state, catalog.entries[index].path, message);
                return true;
            }
            message = "Selección inválida; introduce un número, M o X.";
            return true;
        }

        bool resized = false;
        const auto key = readKeyWithResize(lastSize, resized);
        if (resized) {
            continue;
        }
        if (!key) {
            return true;
        }
        if (key->type == controls::Key::Up) {
            selected = (selected + items.size() - 1) % items.size();
            continue;
        }
        if (key->type == controls::Key::Down) {
            selected = (selected + 1) % items.size();
            continue;
        }
        if (key->type == controls::Key::Char &&
            (key->value == 27 || key->value == 'x' || key->value == 'X')) {
            message = "Selección de patrón cancelada.";
            return true;
        }

        std::size_t index = selected;
        if (key->type == controls::Key::Char && (key->value == 'm' || key->value == 'M')) {
            index = manualIndex;
        } else if (key->type == controls::Key::Char &&
                   key->value >= '1' && key->value <= '9') {
            const std::size_t requested =
                static_cast<std::size_t>(key->value - '1');
            if (requested < catalog.entries.size()) {
                index = requested;
            }
        } else if (key->type != controls::Key::Enter) {
            continue;
        }

        if (index == backIndex) {
            message = "Selección de patrón cancelada.";
            return true;
        }
        if (index == manualIndex) {
            const PathRequest request = askPath();
            if (request.eof) {
                return false;
            }
            if (!request.cancelled) {
                loadInto(state, request.path, message);
            }
            return true;
        }
        if (!catalog.entries[index].valid()) {
            message = "Error al cargar: " + catalog.entries[index].error +
                " (se conserva la entrada anterior)";
            continue;
        }
        loadInto(state, catalog.entries[index].path, message);
        return true;
    }
}

bool ensureInput(AppState& state, std::string& message) {
    if (!state.input) {
        if (controls::isInteractive()) {
            return selectPatternFromCatalog(state, message);
        }
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
    if (controls::isInteractive()) {
        return selectPatternFromCatalog(state, message);
    }
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
    constexpr std::size_t kOptionCount = 5;
    std::size_t selected = 0;
    auto lastSize = controls::terminalSize();
    auto mpiChoice = [&](bool configure) {
        if (configure) {
            const auto value = controls::readLine("Procesos MPI P (1..1024; recomendados " + std::to_string(recommendedMPIProcesses(state.input ? state.input->height : 1024)) + "): ");
            const auto count = value ? parseMPIProcesses(*value) : std::nullopt;
            if (!count) { message = "Error: P debe estar entre 1 y 1024."; return; }
            state.mpiProcesses = *count;
            message = "Procesos MPI: " + std::to_string(*count) + " (sin sobresuscripción).";
        } else {
            state.engine = Engine::MPI;
            message = "Motor seleccionado: MPI";
        }
    };

    while (true) {
        renderEngineMenu(state, selected);

        bool resized = false;
        std::optional<controls::KeyPress> action;
        if (interactive) {
            action = readKeyWithResize(lastSize, resized);
            if (resized) {
                continue;
            }
            if (!action) {
                return true;
            }
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
                    } else if (selected == 2 || selected == 3) {
                        mpiChoice(selected == 3); return true;
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
                case 'm': case 'M': mpiChoice(false); return true;
                case 'p': case 'P': mpiChoice(true); return true;
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
        if (*line == "m" || *line == "M") { mpiChoice(false); return true; }
        if (*line == "p" || *line == "P") { mpiChoice(true); return true; }
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
        header.statusText = session.busy() ? "Operación pendiente" : session.isRunning() ? "En ejecución" : "Pausado";

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
            : state.engine == Engine::MPI ? "MPI | procesos: " + std::to_string(state.mpiProcesses) + " | U recuperar, Q abandonar, X menú"
            : "Motor: Secuencial";

        const std::size_t frameWidth = std::max<std::size_t>(1, terminal.columns);
        const std::size_t frameHeight = std::max<std::size_t>(1, terminal.rows);
        const bool sideBySide = frameWidth >= 86;
        const bool compact = frameWidth < 86 || frameHeight < 18;
        const std::size_t fixedRows = sideBySide ? 13 : 17;
        const bool renderSideBySide = sideBySide && !compact;
        const std::size_t availableBoardRows = std::max<std::size_t>(
            1, compact
                ? (frameHeight > 6 ? (frameHeight - 6) / 2 : 1)
                : (frameHeight > fixedRows
                    ? (sideBySide ? frameHeight - fixedRows : (frameHeight - fixedRows) / 2)
                    : 1));
        const std::size_t cellHeight = std::clamp(
            availableBoardRows / std::max<std::size_t>(1, game.height()),
            std::size_t{1}, std::size_t{3});
        const std::size_t rowsPerCell = std::max<std::size_t>(
            1, availableBoardRows / cellHeight);
        const std::size_t maxBoardRows = renderSideBySide ? 60 : 30;
        const std::size_t visibleRows = std::min(rowsPerCell, maxBoardRows);
        const std::size_t panelWidth = sideBySide
            ? (frameWidth - 6) / 2
            : (frameWidth > 4 ? frameWidth - 4 : 1);
        const std::size_t cellArea = panelWidth > 4 ? panelWidth - 4 : 1;
        const std::size_t cellWidth = std::clamp(
            cellArea / std::max<std::size_t>(1, game.width()), std::size_t{1}, std::size_t{4});
        const std::size_t visibleColumns = std::max<std::size_t>(
            1, (cellArea + 1) / (cellWidth + 1));
        std::size_t boardRows = std::min(game.height(), visibleRows);
        if ((game.height() > boardRows || game.width() > visibleColumns) &&
            boardRows * cellHeight >= availableBoardRows) {
            boardRows = std::max<std::size_t>(
                1, std::min(boardRows, (availableBoardRows - 1) / cellHeight));
        }
        const ui::BoardRenderOptions boardOptions{
            visibleColumns, boardRows, controls::supportsAnsiColors(), cellWidth, cellHeight};

        ui::BoardPanelData current{"CURRENT", ui::renderBoardLines(game, boardOptions)};
        ui::BoardPanelData next = session.preview()
            ? ui::BoardPanelData{"NEXT", ui::renderBoardLines(*session.preview(), boardOptions)}
            : ui::BoardPanelData{"MPI · snapshot confirmado", {session.busy() ? "Operación pendiente" : "Sin vista previa especulativa", session.error()}};
        const ui::SimulationLayoutConfig layout{
            frameWidth,
            std::max(current.lines.size(), next.lines.size()),
            boardOptions.colorEnabled,
            frameHeight
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
            case 27:
                session.pause(); return false;
            case 'u': case 'U': session.recover(); changed = true; return true;
            case 'q': case 'Q': session.close(); changed = true; return true;
            default:
                return true;
        }
}

bool runVisualization(AppState& state) {
        if (state.engine == Engine::OpenMP) {
            omp_set_num_threads(state.requestedOpenMPThreads);
        }
        if (state.simulation) state.simulation->poll();
        if (state.replaceSimulation && state.simulation && !state.simulation->closed()) {
            std::cout << "Cerrando la sesión anterior; vuelve a Visualización al terminar.\n";
            return true;
        }
        if (!state.simulation || state.simulation->closed()) {
            state.simulation = std::make_unique<SimulationSession>(*state.input, state.engine, defaultMPIOptions(state.mpiProcesses));
            state.replaceSimulation = false;
        } else if (state.simulation->engine() != state.engine || state.simulation->processes() != state.mpiProcesses)
            state.simulation->configure(state.engine, state.mpiProcesses);
        auto& session = *state.simulation;
        const bool interactive = controls::isInteractive();
        bool redraw = true;
        controls::TerminalSize lastSize = controls::terminalSize();

        while (true) {
            const bool wasBusy = session.busy();
            session.poll();
            if (wasBusy != session.busy()) redraw = true;
            if (session.closed()) return true;
            const controls::TerminalSize currentSize = controls::terminalSize();
            if (currentSize != lastSize) {
                lastSize = currentSize;
                redraw = true;
            }
            if (redraw) {
                if (interactive) {
                    clearScreen();
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

            // Deliver asynchronous snapshots even while the line-based client
            // is waiting for its next command. Never wait on mpi::Client here.
            if (session.busy() || session.isRunning()) {
                pollfd input{STDIN_FILENO,POLLIN,0};
                if (::poll(&input,1,150) <= 0) {
                    session.advanceIfRunning(); redraw = true; continue;
                }
            }
            const auto line = controls::readLine("");
            if (!line) {
                return false;
            }
            session.poll();
            const char command = line->empty() ? '\0' : (*line)[0];
            if (!applySimulationCommand(command, session, redraw)) {
                return true;
            }
        }
}

bool waitForModuleReturn(bool interactive) {
        if (interactive) {
            return controls::readKey().has_value();
        }
        return controls::readLine("").has_value();
}

bool runBenchmarkUI(AppState& state, std::string& message) {
        if (state.simulation && !state.simulation->closed() &&
            (state.simulation->engine() == Engine::MPI || state.simulation->busy())) {
            message = "Abandona la sesión MPI con Q antes de medir."; return true;
        }
        if (!ensureInput(state, message)) {
            return false;
        }
        if (!state.input) {
            return true;
        }

        constexpr int kGenerations = 20;
        constexpr int kRepetitions = 5;
        clearScreen();
        ui::renderScreen(
            "BENCHMARK EN CURSO",
            "Preparando comparación Secuencial / OpenMP",
            {"Calentamiento y " + std::to_string(kRepetitions) + " repeticiones por motor",
             "La interfaz no forma parte del tiempo medido"},
            "La operación termina automáticamente; espera unos instantes.",
            currentLayoutConfig());
        std::cout << std::flush;
        const BenchmarkResult result = runBenchmark(
            *state.input, kGenerations, kRepetitions, state.requestedOpenMPThreads);

        std::vector<std::string> report;
        report.push_back("Generaciones: " + std::to_string(result.generations) +
            "  |  Repeticiones: " + std::to_string(result.repetitions));
        report.push_back("Hilos lógicos disponibles: " +
            std::to_string(availableLogicalProcessors()) +
            "  |  OpenMP solicitados: " + std::to_string(result.requestedThreads));
        std::string effectiveThreads;
        if (result.effectiveThreadCountStable) {
            effectiveThreads = std::to_string(result.minEffectiveThreads);
        } else {
            effectiveThreads = std::to_string(result.minEffectiveThreads) + "–" +
                std::to_string(result.maxEffectiveThreads) + " (variaron entre regiones)";
        }
        report.push_back("Hilos OpenMP efectivos: " + effectiveThreads);
        std::ostringstream timings;
        timings << std::fixed << std::setprecision(3)
                << "Secuencial     " << result.sequentialMedianMs << " ms";
        report.push_back(timings.str());
        timings.str({});
        timings.clear();
        timings << std::fixed << std::setprecision(3)
                << "OpenMP         " << result.openMPMedianMs << " ms";
        report.push_back(timings.str());
        timings.str({});
        timings.clear();
        timings << std::fixed << std::setprecision(3)
                << "Speedup: " << result.speedup << "×";
        report.push_back(timings.str());
        if (result.efficiencyValid) {
            timings.str({});
            timings.clear();
            timings << std::fixed << std::setprecision(2)
                    << "Eficiencia: " << (result.efficiency * 100.0) << "%";
            report.push_back(timings.str());
        } else {
            report.push_back("Eficiencia: no disponible (hilos variables o resultados distintos)");
        }
        report.push_back("Estados equivalentes: " + std::string(result.equivalent ? "SI" : "NO"));
        if (!result.equivalent) {
            report.push_back("Interpretación: resultados distintos; no comparar rendimiento.");
        } else if (result.openMPMedianMs < result.sequentialMedianMs) {
            report.push_back("Interpretación: OpenMP fue más rápido en esta medición.");
        } else if (result.sequentialMedianMs < result.openMPMedianMs && result.speedup > 0.0) {
            timings.str({});
            timings.clear();
            timings << std::fixed << std::setprecision(2)
                    << "Interpretación: Secuencial fue " << (1.0 / result.speedup)
                    << "× más rápido.";
            report.push_back(timings.str());
        } else {
            report.push_back("Interpretación: tiempos iguales o resolución insuficiente.");
        }
        if (state.input->cells.size() < 256) {
            report.push_back("Nota: tablero pequeño; el overhead OpenMP puede dominar.");
        }
        clearScreen();
        ui::renderScreen(
            "BENCHMARK  ·  SECUENCIAL VS OPENMP",
            "Patrón: " + state.path + "  |  " + std::to_string(state.input->width) +
                " × " + std::to_string(state.input->height),
            report,
            "Presiona una tecla para volver al menú.",
            currentLayoutConfig());
        std::cout << std::flush;

        if (!waitForModuleReturn(controls::isInteractive())) {
            return false;
        }
        if (!result.equivalent) {
            message = "Error: los motores produjeron estados distintos.";
        }
        return true;
}

std::optional<MenuAction> pollMenuAction(
    bool interactive,
    controls::TerminalSize& lastSize
) {
    if (interactive) {
        bool resized = false;
        const auto key = readKeyWithResize(lastSize, resized);
        if (resized) {
            return MenuAction::None;
        }

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
    auto lastSize = controls::terminalSize();

    while (true) {
        renderMenu(state, selected, message);
        message.clear();

        auto action = pollMenuAction(interactive, lastSize);
        if (state.simulation) state.simulation->poll();

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
                if (!selectEngineUI(state, message)) return 0;
                if (state.simulation && !state.simulation->closed() && state.engine != Engine::MPI)
                    state.simulation->configure(state.engine, state.mpiProcesses);
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
        (void)std::setlocale(LC_CTYPE, "");
        if (controls::isInteractive() && controls::isOutputInteractive()) {
            return runFtxuiApp();
        }
        return runLoop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

}

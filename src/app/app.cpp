#include "app/app.hpp"

#include "app/controls.hpp"
#include "core/game.hpp"
#include "input/input.hpp"
#include "ui/layout.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace conway::app {

namespace {

constexpr const char* kClearScreen = "\033[2J\033[H";
constexpr const char* kErrorColor = "\033[1;31m";

// Estado global de la aplicacion. La seleccion de motor se conserva
// aqui (no en Game) para que cambiar de patron no reinicie la
// eleccion. Los modulos de Benchmark/Visualizacion (futuros) deberan
// leer state.engine y llamar game.setEngine(state.engine) en su Game.
struct AppState {
    std::optional<InputData> input;
    std::string path;
    conway::Engine engine{conway::Engine::Sequential};
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
        "Juego de la Vida",
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

bool showPlaceholder(const std::string& title, const AppState& state) {
    std::cout << kClearScreen;
    ui::renderLayout(
        title,
        "Modulo en construccion",
        makeInputInfo(state),
        {{"0", "Volver al menu"}},
        0
    );
    std::cout << "\nPresiona Enter para volver al menu.\n" << std::flush;

    if (controls::isInteractive()) {
        return controls::readKey().has_value();
    }

    return controls::readLine("").has_value();
}

bool openSection(const std::string& title, AppState& state, std::string& message) {
    if (!state.input) {
        const PathRequest request = askPath();

        if (request.eof) {
            return false;
        }

        if (request.cancelled) {
            message = "Operacion cancelada.";
            return true;
        }

        if (!loadInto(state, request.path, message)) {
            return true;
        }
    }

    return showPlaceholder(title, state);
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
            return MenuAction::ChangeInput;
        case 4:
            return MenuAction::Exit;
        default:
            return MenuAction::None;
    }
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
                if (!openSection("BENCHMARK", state, message)) {
                    return 0;
                }
                break;
            case MenuAction::Visualization:
                if (!openSection("VISUALIZACION", state, message)) {
                    return 0;
                }
                break;
            case MenuAction::SelectEngine:
                if (!selectEngineUI(state, message)) {
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

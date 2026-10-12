#include "app/ftxui_app.hpp"

#include "app/benchmark.hpp"
#include "app/controls.hpp"
#include "app/didactic_session.hpp"
#include "app/pattern_catalog.hpp"
#include "app/pattern_preview.hpp"
#include "app/simulation.hpp"
#include "app/thread_config.hpp"
#include "core/engines/openmp.hpp"
#include "input/input.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <omp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <streambuf>
#include <thread>
#include <utility>
#include <vector>

namespace conway::app {

namespace {

using namespace std::chrono_literals;
using ftxui::Element;
using ftxui::Event;

// Forward declarations: los helpers de tablero se usan antes de su
// definicion (didacticBoardRows, didacticCurrentRows) y se mantienen
// en el mismo namespace anonimo.
Element cellElement(bool alive, bool colors);
Element gridElement(std::string value, bool colors);
std::string gridBorder(
    std::size_t columns,
    std::string_view junction,
    std::string_view left,
    std::string_view right);
std::size_t visibleBoardRows(
    std::size_t screenHeight,
    std::size_t boardHeight,
    bool compact);
std::vector<Element> boardRows(
    const conway::Game& game,
    std::size_t rows,
    std::size_t columns,
    bool colors);
ftxui::Color workerColor(int index);
class DifferentialOutputBuffer;

enum class Page {
    Main,
    Patterns,
    Engines,
    Threads,
    PathInput,
    ThreadInput,
    Simulation,
    Benchmark,
    Didactic
};

struct InteractiveState {
    std::optional<InputData> input;
    std::string path;
    std::string message;
    Engine engine{Engine::Sequential};
    int requestedThreads{defaultOpenMPThreadCount()};
    int effectiveThreads{0};
    Page page{Page::Main};
    Page returnAfterCatalog{Page::Main};
    int mainSelection{0};
    int patternSelection{0};
    int patternActionSelection{0};
    int engineSelection{0};
    int threadSelection{0};
    bool patternActionsFocused{false};
    std::string pathDraft;
    std::string threadDraft;
    std::unique_ptr<SimulationSession> simulation;
    std::unique_ptr<DidacticController> didactic;
    std::optional<BenchmarkResult> benchmark;
    bool benchmarkRunning{false};
};

bool colorsEnabled() {
    const char* noColor = std::getenv("NO_COLOR");
    const char* term = std::getenv("TERM");
    return !(noColor && *noColor != '\0') && term && std::string(term) != "dumb";
}

// FTXUI v5 writes a full raster per draw; keep its event loop but emit only
// rows whose rendered pixels changed.
class DifferentialOutputBuffer final : public std::streambuf {
public:
    explicit DifferentialOutputBuffer(std::streambuf* destination)
        : destination_(destination) {}

    void setFrame(std::string frame, int width, int height) {
        frameRows_.clear();
        std::size_t start = 0;
        while (true) {
            const std::size_t end = frame.find("\r\n", start);
            frameRows_.push_back(frame.substr(start, end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 2;
        }
        width_ = width;
        height_ = height;
        framePending_ = true;
    }

protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        pending_.push_back(traits_type::to_char_type(character));
        return character;
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override {
        pending_.append(data, static_cast<std::size_t>(size));
        return size;
    }

    int sync() override {
        bool written = true;
        if (framePending_) {
            written = drawFrame();
            if (written) {
                framePending_ = false;
                pending_.clear();
            }
        } else if (!pending_.empty()) {
            const auto size = static_cast<std::streamsize>(pending_.size());
            written = destination_->sputn(pending_.data(), size) == size;
            if (written) {
                pending_.clear();
            }
        }
        const int flushStatus = destination_->pubsync();
        return written && flushStatus == 0 ? 0 : -1;
    }

private:
    bool drawFrame() {
        const bool resized = width_ != previousWidth_ || height_ != previousHeight_;
        std::string output;
        if (resized && !previousRows_.empty()) {
            output += "\x1b[2J";
        }
        for (std::size_t row = 0; row < frameRows_.size(); ++row) {
            if (!resized && row < previousRows_.size() &&
                frameRows_[row] == previousRows_[row]) {
                continue;
            }
            output += "\x1b[" + std::to_string(row + 1) + ";1H";
            output += frameRows_[row];
            // Rows span the terminal width; EL here can erase the last cell.
            output += "\x1b[0m";
        }
        if (previousRows_.empty()) {
            output += "\x1b[?25l";
        }
        const auto size = static_cast<std::streamsize>(output.size());
        if (destination_->sputn(output.data(), size) != size) {
            return false;
        }
        previousRows_ = frameRows_;
        previousWidth_ = width_;
        previousHeight_ = height_;
        return true;
    }

    std::streambuf* destination_;
    std::string pending_;
    std::vector<std::string> frameRows_;
    std::vector<std::string> previousRows_;
    int width_{0};
    int height_{0};
    int previousWidth_{0};
    int previousHeight_{0};
    bool framePending_{false};
};

std::string engineName(Engine engine) {
    return engine == Engine::Sequential ? "Secuencial" : "OpenMP";
}

std::string activePattern(const InteractiveState& state) {
    if (!state.input) {
        return "Sin patrón cargado";
    }
    return state.path + "  ·  " + std::to_string(state.input->height) + " × " +
        std::to_string(state.input->width);
}

Element heading(std::string label) {
    Element output = ftxui::text(std::move(label)) | ftxui::bold;
    return colorsEnabled() ? output | ftxui::color(ftxui::Color::Cyan) : output;
}

Element panel(std::string title, Element body) {
    return ftxui::vbox({
        heading(std::move(title)),
        ftxui::separator(),
        std::move(body) | ftxui::flex,
    }) | ftxui::border | ftxui::flex;
}

Element cellElement(bool alive, bool colors) {
    if (!colors) {
        return ftxui::text(alive ? "▓▓" : "░░");
    }
    const auto background = alive
        ? ftxui::Color::RGB(98, 232, 137)
        : ftxui::Color::RGB(38, 49, 56);
    return ftxui::text("  ") | ftxui::bgcolor(background);
}

// Color de un worker para la visualizacion didactica. Mantiene el
// verde #62E889 para celulas vivas (no se usa aqui) y elige una
// paleta distinguible para el resto: cada worker tiene un color
// propio que se aplica a sus celdas reveladas (muertas) y a su
// marca en la lista inferior.
ftxui::Color workerColor(int index) {
    static const ftxui::Color palette[] = {
        ftxui::Color::RGB(0xE8, 0x9E, 0x5A),  // T0 naranja
        ftxui::Color::RGB(0x5A, 0x8F, 0xE8),  // T1 azul
        ftxui::Color::RGB(0xB4, 0x5A, 0xE8),  // T2 morado
        ftxui::Color::RGB(0xE8, 0xD8, 0x5A),  // T3 amarillo
        ftxui::Color::RGB(0x5A, 0xE8, 0xC8),  // T4 verde-azulado
        ftxui::Color::RGB(0xE8, 0x5A, 0x9E),  // T5 rosa
        ftxui::Color::RGB(0xE8, 0x5A, 0x5A),  // T6 rojo
        ftxui::Color::RGB(0x8A, 0x6A, 0xE8),  // T7 indigo
    };
    if (index < 0) return ftxui::Color::RGB(0x1F, 0x2A, 0x30);
    return palette[index % 8];
}

// Celda didactica. alive siempre se pinta verde (#62E889). Las celdas
// muertas usan el color del worker que las revelo (o dim gris si
// no han sido reveladas). Asi el reparto se ve en las celdas
// muertas; las celdas vivas se distinguen por su color canonico.
Element didacticCellElement(bool alive, bool revealed,
                             std::int8_t worker_index, bool colors) {
    if (!revealed) {
        return ftxui::text("  ") |
            ftxui::bgcolor(ftxui::Color::RGB(0x1F, 0x2A, 0x30));
    }
    if (!colors) {
        return ftxui::text(alive ? "▓▓" : "░░");
    }
    if (alive) {
        return ftxui::text("  ") | ftxui::bgcolor(ftxui::Color::RGB(98, 232, 137));
    }
    return ftxui::text("  ") | ftxui::bgcolor(workerColor(worker_index));
}

// Construye el tablero NEXT del panel didactico, usando la mascara
// de revelado del frame. Las celdas no reveladas se pintan dim;
// las reveladas usan el color del worker.
std::vector<Element> didacticBoardRows(
    const DidacticFrame& frame,
    std::size_t visibleRows,
    std::size_t visibleColumns,
    bool colors
) {
    std::vector<Element> output;
    if (!frame.has_data || frame.width == 0 || frame.height == 0) {
        return output;
    }
    if (visibleRows == 0 || visibleColumns == 0) {
        return output;
    }
    const std::size_t rows = std::min(visibleRows, frame.height);
    const std::size_t columns = std::min(visibleColumns, frame.width);
    output.reserve(rows * 2 + 3);
    output.push_back(gridElement(
        gridBorder(columns, "┬", "┌", "┐"), colors));
    for (std::size_t row = 0; row < rows; ++row) {
        std::vector<Element> cells;
        cells.reserve(columns * 2 + 1);
        cells.push_back(gridElement("│", colors));
        for (std::size_t column = 0; column < columns; ++column) {
            const std::size_t idx = row * frame.width + column;
            const bool alive = frame.next_alive_map[idx] != 0;
            const bool revealed = frame.revealed[idx] != 0;
            const std::int8_t wid =
                revealed ? frame.worker_id[idx] : std::int8_t{-1};
            cells.push_back(didacticCellElement(alive, revealed, wid, colors));
            cells.push_back(gridElement("│", colors));
        }
        output.push_back(ftxui::hbox(std::move(cells)));
        if (row + 1 < rows) {
            output.push_back(gridElement(
                gridBorder(columns, "┼", "├", "┤"), colors));
        } else {
            output.push_back(gridElement(
                gridBorder(columns, "┴", "└", "┘"), colors));
        }
    }
    if (rows < frame.height || columns < frame.width) {
        output.push_back(ftxui::text(
            "Viewport: " + std::to_string(rows) + "/" +
            std::to_string(frame.height) + " filas, " +
            std::to_string(columns) + "/" + std::to_string(frame.width) +
            " columnas") | ftxui::dim);
    }
    return output;
}

// CURRENT del panel didactico. Mismas dimensiones que la salida de
// didacticBoardRows, pero sin mascara: todo esta siempre visible.
std::vector<Element> didacticCurrentRows(
    const DidacticFrame& frame,
    std::size_t visibleRows,
    std::size_t visibleColumns,
    bool colors
) {
    std::vector<Element> output;
    if (!frame.has_data || frame.width == 0 || frame.height == 0) {
        return output;
    }
    if (visibleRows == 0 || visibleColumns == 0) {
        return output;
    }
    const std::size_t rows = std::min(visibleRows, frame.height);
    const std::size_t columns = std::min(visibleColumns, frame.width);
    output.reserve(rows * 2 + 3);
    output.push_back(gridElement(
        gridBorder(columns, "┬", "┌", "┐"), colors));
    for (std::size_t row = 0; row < rows; ++row) {
        std::vector<Element> cells;
        cells.reserve(columns * 2 + 1);
        cells.push_back(gridElement("│", colors));
        for (std::size_t column = 0; column < columns; ++column) {
            const std::size_t idx = row * frame.width + column;
            const bool alive = frame.current_alive_map[idx] != 0;
            cells.push_back(cellElement(alive, colors));
            cells.push_back(gridElement("│", colors));
        }
        output.push_back(ftxui::hbox(std::move(cells)));
        if (row + 1 < rows) {
            output.push_back(gridElement(
                gridBorder(columns, "┼", "├", "┤"), colors));
        } else {
            output.push_back(gridElement(
                gridBorder(columns, "┴", "└", "┘"), colors));
        }
    }
    return output;
}

Element gridElement(std::string value, bool colors) {
    Element output = ftxui::text(std::move(value));
    return colors
        ? output | ftxui::color(ftxui::Color::RGB(88, 103, 111))
        : output;
}

std::string gridBorder(
    std::size_t columns,
    std::string_view junction,
    std::string_view left,
    std::string_view right
) {
    std::string output(left);
    for (std::size_t column = 0; column < columns; ++column) {
        output += "──";
        if (column + 1 < columns) {
            output += junction;
        }
    }
    output += right;
    return output;
}

std::vector<Element> boardRows(
    const Game& game,
    std::size_t rows,
    std::size_t columns,
    bool colors
) {
    std::vector<Element> output;
    const std::size_t visibleRows = std::min(rows, game.height());
    const std::size_t visibleColumns = std::min(columns, game.width());
    if (visibleRows == 0 || visibleColumns == 0) {
        return output;
    }
    output.reserve(visibleRows * 2 + 3);
    output.push_back(gridElement(
        gridBorder(visibleColumns, "┬", "┌", "┐"), colors));
    for (std::size_t row = 0; row < visibleRows; ++row) {
        std::vector<Element> cells;
        cells.reserve(visibleColumns * 2 + 1);
        cells.push_back(gridElement("│", colors));
        for (std::size_t column = 0; column < visibleColumns; ++column) {
            cells.push_back(cellElement(game.isAlive(row, column), colors));
            cells.push_back(gridElement("│", colors));
        }
        output.push_back(ftxui::hbox(std::move(cells)));
        if (row + 1 < visibleRows) {
            output.push_back(gridElement(
                gridBorder(visibleColumns, "┼", "├", "┤"), colors));
        } else {
            output.push_back(gridElement(
                gridBorder(visibleColumns, "┴", "└", "┘"), colors));
        }
    }
    if (visibleRows < game.height() || visibleColumns < game.width()) {
        output.push_back(ftxui::text(
            "Viewport: " + std::to_string(visibleRows) + "/" +
            std::to_string(game.height()) + " filas, " +
            std::to_string(visibleColumns) + "/" + std::to_string(game.width()) +
            " columnas") | ftxui::dim);
    }
    return output;
}

std::size_t visibleBoardRows(
    std::size_t screenHeight,
    std::size_t boardHeight,
    bool compact
) {
    const std::size_t rowBudget = screenHeight > 17 ? screenHeight - 17 : 1;
    const std::size_t boardRowBudget = compact ? rowBudget / 4 : rowBudget / 2;
    return std::max<std::size_t>(
        1, std::min(boardHeight, boardRowBudget));
}

std::string benchmarkReport(const BenchmarkResult& result) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << "Mediana secuencial: " << result.sequentialMedianMs << " ms\n"
           << "Mediana OpenMP:     " << result.openMPMedianMs << " ms\n"
           << "Speedup:             " << result.speedup << "x\n"
           << "Hilos solicitados:   " << result.requestedThreads << "\n"
           << "Hilos efectivos:     ";
    if (result.effectiveThreadCountStable) {
        output << result.minEffectiveThreads;
    } else {
        output << result.minEffectiveThreads << " - " << result.maxEffectiveThreads
               << " (variaron; eficiencia no representativa)";
    }
    output << "\nEficiencia:          ";
    if (result.efficiencyValid) {
        output << result.efficiency * 100.0 << "%";
    } else {
        output << "no disponible";
    }
    output << "\nEquivalencia:        " << (result.equivalent ? "correcta" : "ERROR")
           << "\nGeneraciones:        " << result.generations
           << "\nRepeticiones:        " << result.repetitions;
    if (result.openMPMedianMs > 0.0 && result.speedup > 0.0) {
        if (result.speedup > 1.0) {
            output << "\nOpenMP fue " << result.speedup << "x más rápido.";
        } else if (result.speedup < 1.0) {
            output << "\nSecuencial fue " << (1.0 / result.speedup) << "x más rápido.";
        } else {
            output << "\nAmbos motores tuvieron tiempos medianos similares.";
        }
    }
    return output.str();
}

class InteractiveApp final : public ftxui::ComponentBase {
public:
    InteractiveApp(ftxui::ScreenInteractive& screen, DifferentialOutputBuffer& output)
        : screen_(screen),
          output_(output),
          mainMenu_(makeMenu(mainItems_, state_.mainSelection, [this] { selectMain(); })),
          patternMenu_(makeMenu(patternItems_, state_.patternSelection,
                                 [this] { selectPattern(); },
                                 [this] { updateCatalogPreview(); })),
          patternActionsMenu_(makeMenu(
              patternActionItems_, state_.patternActionSelection,
              [this] { selectPatternAction(); })),
          engineMenu_(makeMenu(engineItems_, state_.engineSelection,
                               [this] { selectEngine(); })),
          threadMenu_(makeMenu(threadItems_, state_.threadSelection,
                               [this] { selectThreadOption(); })),
          pathInput_(makePathInput()),
          threadInput_(makeThreadInput()) {
        omp_set_num_threads(state_.requestedThreads);
        Add(mainMenu_);
        Add(patternMenu_);
        Add(patternActionsMenu_);
        Add(engineMenu_);
        Add(threadMenu_);
        Add(pathInput_);
        Add(threadInput_);
        Add(simulationView_);
        Add(benchmarkView_);
        Add(didacticView_);
        mainItems_ = {
            "Benchmark",
            "Visualización",
            "Seleccionar motor",
            "Configurar hilos OpenMP",
            "Catálogo de patrones",
            "Visualización didáctica",
            "Salir",
        };
        engineItems_ = {"Secuencial", "OpenMP", "Volver"};
        threadItems_ = {
            "1 hilo", "2 hilos", "4 hilos", "8 hilos",
            "Número personalizado", "Volver",
        };
        refreshPatterns();
    }

    ~InteractiveApp() override {
        playbackTimer_.request_stop();
        // Destruir el didactic antes que el worker thread termine,
        // para que joinWorker() se ejecute y no quede colgando.
        state_.didactic.reset();
        if (benchmarkWorker_.joinable()) {
            benchmarkWorker_.join();
        }
    }

    Element Render() override {
        const auto size = controls::terminalSize();
        const bool compact = size.columns < 64 || size.rows < 18;
        Element content;
        switch (state_.page) {
            case Page::Main:
                content = renderMain();
                break;
            case Page::Patterns:
                content = renderPatterns(size.columns, size.rows, compact);
                break;
            case Page::Engines:
                content = renderEngines();
                break;
            case Page::Threads:
                content = renderThreads();
                break;
            case Page::PathInput:
                content = renderInput("CARGAR PATRÓN", pathInput_,
                                      "Ruta relativa o absoluta al archivo .txt");
                break;
            case Page::ThreadInput:
                content = renderInput("HILOS OPENMP", threadInput_,
                                      "Número entero entre 1 y " +
                                          std::to_string(kMaxOpenMPThreads));
                break;
            case Page::Simulation:
                content = renderSimulation(size.columns, size.rows, compact);
                break;
            case Page::Benchmark:
                content = renderBenchmark();
                break;
            case Page::Didactic:
                content = renderDidactic(size.columns, size.rows, compact);
                break;
        }

        Element top = ftxui::vbox({
            ftxui::hbox({
                heading("CONWAY PARALLEL"),
                ftxui::filler(),
                ftxui::text(engineName(state_.engine) + "  |  OpenMP solicitado: " +
                            std::to_string(state_.requestedThreads)) | ftxui::dim,
            }),
            ftxui::text(activePattern(state_)) | ftxui::dim,
        });
        Element bottom = ftxui::hbox({
            ftxui::text(state_.message) | ftxui::color(state_.message.starts_with("Error")
                ? ftxui::Color::Red : ftxui::Color::Green),
            ftxui::filler(),
            ftxui::text(footerText()) | ftxui::dim,
        });
        Element frame = ftxui::vbox({
            std::move(top) | ftxui::border,
            std::move(content) | ftxui::flex,
            std::move(bottom) | ftxui::border,
        });
        const int width = static_cast<int>(size.columns);
        const int height = static_cast<int>(size.rows);
        ftxui::Screen raster = ftxui::Screen::Create(
            ftxui::Dimensions{width, height});
        ftxui::Render(raster, frame);
        output_.setFrame(raster.ToString(), width, height);
        return frame;
    }

    ftxui::Component ActiveChild() override {
        return activeComponent();
    }

    bool OnEvent(Event event) override {
        if (event == Event::Custom) {
            const unsigned pending = pendingEvents_.exchange(0, std::memory_order_acq_rel);
            if ((pending & kBenchmarkEvent) != 0) {
                receiveBenchmarkResult();
            }
            if ((pending & kPlaybackEvent) != 0) {
                if (state_.page == Page::Simulation &&
                    state_.simulation && state_.simulation->isRunning()) {
                    state_.simulation->advanceIfRunning();
                    state_.effectiveThreads = state_.engine == Engine::OpenMP
                        ? core::last_openmp_team_size() : 1;
                    schedulePlaybackTick();
                } else if (state_.page == Page::Didactic && state_.didactic &&
                           state_.didactic->isPlaying()) {
                    // El tick avanza el reloj de reproduccion
                    // didactica con 150 ms de tiempo simulado.
                    state_.didactic->tick(std::chrono::milliseconds(150));
                    if (state_.didactic->isPlaying()) {
                        schedulePlaybackTick();
                    }
                }
            }
            return true;
        }
        if (event == Event::Escape) {
            goBack();
            return true;
        }
        if (state_.page == Page::Patterns &&
            handlePatternNavigationBoundary(event)) {
            return true;
        }
        if (event.is_character()) {
            const std::string key = event.character();
            if (handleShortcut(key)) {
                return true;
            }
        }
        if (state_.page == Page::Simulation && event.is_character()) {
            const std::string key = event.character();
            if (key == "x" || key == "X" || key == "q" || key == "Q") {
                goBack();
                return true;
            }
            if (key == "n" || key == "N") {
                state_.simulation->step();
                state_.effectiveThreads = state_.engine == Engine::OpenMP
                    ? core::last_openmp_team_size() : 1;
                return true;
            }
            if (key == " " || key == "p" || key == "P") {
                state_.simulation->toggleRunning();
                if (state_.simulation->isRunning()) {
                    schedulePlaybackTick();
                } else {
                    playbackTimer_.request_stop();
                }
                return true;
            }
            if (key == "r" || key == "R") {
                state_.simulation->reset();
                state_.effectiveThreads = 0;
                return true;
            }
        }
        if (state_.page == Page::Didactic && event.is_character()) {
            const std::string key = event.character();
            if (key == "x" || key == "X" || key == "q" || key == "Q") {
                goBack();
                return true;
            }
            if (key == "n" || key == "N") {
                if (state_.didactic && state_.didactic->isReady()) {
                    state_.didactic->advanceGeneration();
                }
                return true;
            }
            if (key == " " || key == "p" || key == "P") {
                if (state_.didactic && state_.didactic->isReady()) {
                    state_.didactic->togglePlay();
                    if (state_.didactic->isPlaying()) {
                        schedulePlaybackTick();
                    } else {
                        playbackTimer_.request_stop();
                    }
                }
                return true;
            }
            if (key == "r" || key == "R") {
                if (state_.didactic) {
                    playbackTimer_.request_stop();
                    state_.didactic->resetToInitial();
                }
                return true;
            }
            if (key == "+" || key == "=") {
                if (state_.didactic) state_.didactic->increaseSpeed();
                return true;
            }
            if (key == "-" || key == "_") {
                if (state_.didactic) state_.didactic->decreaseSpeed();
                return true;
            }
        }
        if (state_.page == Page::Benchmark && event == Event::Return &&
            !state_.benchmarkRunning) {
            state_.page = Page::Main;
            return true;
        }
        if (event == Event::Return && state_.page == Page::PathInput) {
            loadPathDraft();
            return true;
        }
        if (event == Event::Return && state_.page == Page::ThreadInput) {
            applyThreadDraft();
            return true;
        }
        return activeComponent()->OnEvent(event);
    }

private:
    static constexpr unsigned kPlaybackEvent = 1U;
    static constexpr unsigned kBenchmarkEvent = 2U;

    void schedulePlaybackTick() {
        playbackTimer_.request_stop();
        playbackTimer_ = std::jthread([this](std::stop_token stopToken) {
            std::this_thread::sleep_for(150ms);
            if (!stopToken.stop_requested()) {
                pendingEvents_.fetch_or(kPlaybackEvent, std::memory_order_release);
                screen_.PostEvent(Event::Custom);
            }
        });
    }

    static ftxui::Component makeMenu(
        std::vector<std::string>& items,
        int& selection,
        std::function<void()> onEnter,
        std::function<void()> onChange = {}
    ) {
        ftxui::MenuOption option;
        option.focused_entry = &selection;
        option.on_enter = std::move(onEnter);
        option.on_change = std::move(onChange);
        return ftxui::Menu(&items, &selection, std::move(option));
    }

    ftxui::Component makePathInput() {
        ftxui::InputOption option;
        option.multiline = false;
        option.placeholder = "patterns/blinker.txt";
        return ftxui::Input(&state_.pathDraft, std::move(option));
    }

    ftxui::Component makeThreadInput() {
        ftxui::InputOption option;
        option.multiline = false;
        option.placeholder = "1-64";
        return ftxui::Input(&state_.threadDraft, std::move(option));
    }

    ftxui::Component activeComponent() const {
        switch (state_.page) {
            case Page::Main: return mainMenu_;
            case Page::Patterns:
                return state_.patternActionsFocused
                    ? patternActionsMenu_ : patternMenu_;
            case Page::Engines: return engineMenu_;
            case Page::Threads: return threadMenu_;
            case Page::PathInput: return pathInput_;
            case Page::ThreadInput: return threadInput_;
            case Page::Simulation: return simulationView_;
            case Page::Benchmark: return benchmarkView_;
            case Page::Didactic: return didacticView_;
        }
        return mainMenu_;
    }

    void refreshPatterns() {
        std::string previousPath;
        if (state_.patternSelection >= 0 &&
            static_cast<std::size_t>(state_.patternSelection) <
                catalog_.entries.size()) {
            previousPath = catalog_.entries[static_cast<std::size_t>(
                state_.patternSelection)].path;
        }
        catalog_ = discoverPatterns("patterns");
        patternItems_.clear();
        patternItems_.reserve(std::max<std::size_t>(1, catalog_.entries.size()));
        for (const PatternEntry& entry : catalog_.entries) {
            std::string label = entry.name;
            if (entry.valid()) {
                label += "  ·  " + std::to_string(entry.height) + " × " +
                    std::to_string(entry.width);
                if (!entry.type.empty()) {
                    label += "  ·  " + entry.type;
                }
            } else {
                label += "  ·  inválido: " + entry.error;
            }
            patternItems_.push_back(std::move(label));
        }
        if (patternItems_.empty()) {
            patternItems_.push_back("No hay patrones .txt disponibles");
            state_.patternSelection = 0;
        } else if (!previousPath.empty()) {
            state_.patternSelection = 0;
            for (std::size_t i = 0; i < catalog_.entries.size(); ++i) {
                if (catalog_.entries[i].path == previousPath) {
                    state_.patternSelection = static_cast<int>(i);
                    break;
                }
            }
        } else if (state_.input) {
            for (std::size_t i = 0; i < catalog_.entries.size(); ++i) {
                if (catalog_.entries[i].path == state_.path) {
                    state_.patternSelection = static_cast<int>(i);
                    break;
                }
            }
        }
        patternActionItems_ = {"Ruta manual...", "Volver"};
        state_.patternActionSelection = 0;
        state_.patternActionsFocused = false;
        updateCatalogPreview();
    }

    bool handlePatternNavigationBoundary(const Event& event) {
        if (event == Event::ArrowDown && !state_.patternActionsFocused &&
            !patternItems_.empty() &&
            state_.patternSelection ==
                static_cast<int>(patternItems_.size() - 1)) {
            state_.patternActionsFocused = true;
            state_.patternActionSelection = 0;
            return true;
        }
        if (event == Event::ArrowUp && state_.patternActionsFocused &&
            state_.patternActionSelection == 0 && !patternItems_.empty()) {
            state_.patternActionsFocused = false;
            state_.patternSelection =
                static_cast<int>(patternItems_.size() - 1);
            updateCatalogPreview();
            return true;
        }
        if (event == Event::ArrowDown && state_.patternActionsFocused &&
            state_.patternActionSelection ==
                static_cast<int>(patternActionItems_.size() - 1) &&
            !patternItems_.empty()) {
            state_.patternActionsFocused = false;
            state_.patternSelection = 0;
            updateCatalogPreview();
            return true;
        }
        if (event == Event::ArrowUp && !state_.patternActionsFocused &&
            state_.patternSelection == 0 && !patternItems_.empty()) {
            state_.patternActionsFocused = true;
            state_.patternActionSelection =
                static_cast<int>(patternActionItems_.size() - 1);
            return true;
        }
        return false;
    }

    void updateCatalogPreview() {
        catalogPreview_ = {};
        if (!catalog_.error.empty()) {
            catalogPreview_.error = catalog_.error;
            return;
        }
        if (catalog_.entries.empty() || state_.patternSelection < 0 ||
            static_cast<std::size_t>(state_.patternSelection) >=
                catalog_.entries.size()) {
            catalogPreview_.error = "No hay un patrón seleccionado.";
            return;
        }
        catalogPreview_ = buildPatternPreview(
            catalog_.entries[static_cast<std::size_t>(state_.patternSelection)]);
    }

    void selectMain() {
        switch (state_.mainSelection) {
            case 0: startBenchmark(); break;
            case 1: startSimulation(); break;
            case 2: state_.page = Page::Engines; break;
            case 3: state_.page = Page::Threads; break;
            case 4:
                state_.returnAfterCatalog = Page::Main;
                refreshPatterns();
                state_.page = Page::Patterns;
                break;
            case 5: startDidactic(); break;
            case 6: screen_.Exit(); break;
            default: break;
        }
    }

    void selectPattern() {
        if (catalog_.entries.empty() || state_.patternSelection < 0 ||
            static_cast<std::size_t>(state_.patternSelection) >=
                catalog_.entries.size()) {
            return;
        }
        const PatternEntry& entry =
            catalog_.entries[static_cast<std::size_t>(state_.patternSelection)];
        if (!entry.valid()) {
            state_.message = "Error: " + entry.error;
            return;
        }
        loadPath(entry.path);
        if (state_.input) {
            state_.page = state_.returnAfterCatalog;
            if (state_.page == Page::Simulation) {
                createSimulation();
            } else if (state_.page == Page::Benchmark) {
                startBenchmark();
            } else if (state_.page == Page::Didactic) {
                createDidactic();
            }
        }
    }

    void selectPatternAction() {
        if (state_.patternActionSelection == 0) {
            state_.pathDraft.clear();
            pathReturnPage_ = Page::Patterns;
            destinationAfterPath_ = state_.returnAfterCatalog;
            state_.page = Page::PathInput;
            return;
        }
        state_.page = state_.returnAfterCatalog;
    }

    void selectEngine() {
        if (state_.engineSelection == 2) {
            state_.page = Page::Main;
            return;
        }
        state_.engine = state_.engineSelection == 0
            ? Engine::Sequential : Engine::OpenMP;
        state_.message = "Motor seleccionado: " + engineName(state_.engine);
        state_.page = Page::Main;
    }

    void selectThreadOption() {
        if (state_.threadSelection == 5) {
            state_.page = Page::Main;
            return;
        }
        if (state_.threadSelection == 4) {
            state_.threadDraft.clear();
            state_.page = Page::ThreadInput;
            return;
        }
        static constexpr int presets[] = {1, 2, 4, 8};
        state_.requestedThreads = presets[state_.threadSelection];
        omp_set_num_threads(state_.requestedThreads);
        state_.message = "Hilos OpenMP solicitados: " +
            std::to_string(state_.requestedThreads);
        state_.page = Page::Main;
    }

    void loadPathDraft() {
        if (state_.pathDraft.empty()) {
            state_.message = "Error: introduce una ruta de archivo.";
            return;
        }
        loadPath(state_.pathDraft);
        if (state_.input) {
            state_.page = destinationAfterPath_;
            if (state_.page == Page::Simulation) {
                createSimulation();
            } else if (state_.page == Page::Benchmark) {
                startBenchmark();
            } else if (state_.page == Page::Didactic) {
                createDidactic();
            } else {
                refreshPatterns();
            }
        }
    }

    void applyThreadDraft() {
        const auto count = parseOpenMPThreadCount(state_.threadDraft);
        if (!count) {
            state_.message = "Error: introduce un entero entre 1 y " +
                std::to_string(kMaxOpenMPThreads) + ".";
            return;
        }
        state_.requestedThreads = *count;
        omp_set_num_threads(state_.requestedThreads);
        state_.message = "Hilos OpenMP solicitados: " +
            std::to_string(state_.requestedThreads);
        state_.page = Page::Main;
    }

    void loadPath(const std::string& path) {
        try {
            InputData loaded = loadInput(path);
            state_.input = std::move(loaded);
            state_.path = path;
            state_.message = "Entrada cargada: " + path;
        } catch (const std::exception& error) {
            state_.message = "Error al cargar " + path + ": " + error.what();
        }
    }

    void startSimulation() {
        if (!state_.input) {
            state_.returnAfterCatalog = Page::Simulation;
            refreshPatterns();
            state_.page = Page::Patterns;
            return;
        }
        createSimulation();
    }

    void createSimulation() {
        state_.simulation = std::make_unique<SimulationSession>(*state_.input, state_.engine);
        state_.effectiveThreads = 0;
        state_.page = Page::Simulation;
    }

    void startDidactic() {
        if (!state_.input) {
            state_.returnAfterCatalog = Page::Didactic;
            refreshPatterns();
            state_.page = Page::Patterns;
            return;
        }
        createDidactic();
    }

    void createDidactic() {
        if (!state_.input) return;
        // El controlador prepara una generacion instrumentada en un
        // worker (no bloquea la UI). Mientras prepara, la pantalla
        // muestra "Preparando generacion...".
        state_.didactic = std::make_unique<DidacticController>();
        state_.didactic->setInitialInput(*state_.input);
        state_.didactic->startPreparation(*state_.input, state_.engine,
                                         state_.requestedThreads);
        state_.page = Page::Didactic;
    }

    void startBenchmark() {
        if (state_.benchmarkRunning) {
            state_.page = Page::Benchmark;
            return;
        }
        if (!state_.input) {
            state_.returnAfterCatalog = Page::Benchmark;
            refreshPatterns();
            state_.page = Page::Patterns;
            return;
        }
        if (benchmarkWorker_.joinable()) {
            benchmarkWorker_.join();
        }
        const InputData snapshot = *state_.input;
        const int threads = state_.requestedThreads;
        state_.benchmark.reset();
        state_.benchmarkRunning = true;
        state_.message.clear();
        state_.page = Page::Benchmark;
        benchmarkWorker_ = std::thread([this, snapshot, threads] {
            try {
                BenchmarkResult result = runBenchmark(snapshot, 20, 5, threads);
                {
                    std::lock_guard lock(benchmarkMutex_);
                    benchmarkResult_ = std::move(result);
                    benchmarkError_.clear();
                }
            } catch (const std::exception& error) {
                std::lock_guard lock(benchmarkMutex_);
                benchmarkResult_.reset();
                benchmarkError_ = error.what();
            }
            pendingEvents_.fetch_or(kBenchmarkEvent, std::memory_order_release);
            screen_.PostEvent(Event::Custom);
        });
    }

    void receiveBenchmarkResult() {
        if (!state_.benchmarkRunning) {
            return;
        }
        if (benchmarkWorker_.joinable()) {
            benchmarkWorker_.join();
        }
        {
            std::lock_guard lock(benchmarkMutex_);
            state_.benchmark = std::move(benchmarkResult_);
            if (!benchmarkError_.empty()) {
                state_.message = "Error en benchmark: " + benchmarkError_;
            } else {
                state_.message = "Benchmark finalizado.";
            }
            benchmarkError_.clear();
        }
        state_.benchmarkRunning = false;
    }

    bool handleShortcut(const std::string& key) {
        if (state_.page == Page::Main) {
            if (key == "0" || key == "q" || key == "Q") {
                screen_.Exit();
                return true;
            }
            if (key == "c" || key == "C") {
                state_.returnAfterCatalog = Page::Main;
                refreshPatterns();
                state_.page = Page::Patterns;
                return true;
            }
            if (key == "1") { startBenchmark(); return true; }
            if (key == "2") { startSimulation(); return true; }
            if (key == "3") { state_.page = Page::Engines; return true; }
            if (key == "4") { state_.page = Page::Threads; return true; }
        }
        if (state_.page == Page::Patterns) {
            if (key == "m" || key == "M") {
                state_.pathDraft.clear();
                pathReturnPage_ = Page::Patterns;
                destinationAfterPath_ = state_.returnAfterCatalog;
                state_.page = Page::PathInput;
                return true;
            }
            if (key == "x" || key == "X" || key == "0") {
                state_.page = state_.returnAfterCatalog;
                return true;
            }
        }
        if (state_.page == Page::Engines) {
            if (key == "s" || key == "S") {
                state_.engineSelection = 0;
                selectEngine();
                return true;
            }
            if (key == "o" || key == "O") {
                state_.engineSelection = 1;
                selectEngine();
                return true;
            }
        }
        if (state_.page == Page::Threads) {
            if (key == "1" || key == "2" || key == "4" || key == "8") {
                state_.threadSelection = key == "1" ? 0 : key == "2" ? 1 :
                    key == "4" ? 2 : 3;
                selectThreadOption();
                return true;
            }
            if (key == "c" || key == "C") {
                state_.threadSelection = 4;
                selectThreadOption();
                return true;
            }
        }
        if (state_.page == Page::Benchmark && (key == "x" || key == "X" || key == "q" || key == "Q")) {
            goBack();
            return true;
        }
        return false;
    }

    void goBack() {
        switch (state_.page) {
            case Page::Main:
                screen_.Exit();
                break;
            case Page::Patterns:
                state_.page = state_.returnAfterCatalog;
                break;
            case Page::Engines:
            case Page::Threads:
            case Page::Benchmark:
                state_.page = Page::Main;
                break;
            case Page::PathInput:
                state_.page = pathReturnPage_;
                break;
            case Page::ThreadInput:
                state_.page = Page::Threads;
                break;
            case Page::Simulation:
                if (state_.simulation && state_.simulation->isRunning()) {
                    state_.simulation->toggleRunning();
                }
                playbackTimer_.request_stop();
                state_.page = Page::Main;
                break;
            case Page::Didactic:
                playbackTimer_.request_stop();
                if (state_.didactic) {
                    state_.didactic->pause();
                }
                state_.page = Page::Main;
                break;
        }
    }

    Element renderMain() {
        return panel("MENÚ PRINCIPAL",
            ftxui::vbox({
                mainMenu_->Render() | ftxui::flex,
                ftxui::separator(),
                ftxui::text("Motor: " + engineName(state_.engine) +
                            "  ·  Hilos OpenMP: " +
                            std::to_string(state_.requestedThreads)),
            }));
    }

    Element renderPatterns(std::size_t width, std::size_t height, bool compact) {
        Element listBody = ftxui::vbox({
            ftxui::text("Archivos .txt disponibles") | ftxui::dim,
            catalog_.error.empty()
                ? patternMenu_->Render() | ftxui::flex
                : ftxui::text("Error: " + catalog_.error) | ftxui::color(ftxui::Color::Red),
            ftxui::separator(),
            ftxui::text("ACCIONES") | ftxui::bold,
            patternActionsMenu_->Render(),
        });
        Element patternList = panel("PATRONES", std::move(listBody));

        std::vector<Element> details;
        if (catalog_.error.empty() && !catalog_.entries.empty() &&
            state_.patternSelection >= 0 &&
            static_cast<std::size_t>(state_.patternSelection) <
                catalog_.entries.size()) {
            const PatternEntry& selected = catalog_.entries[
                static_cast<std::size_t>(state_.patternSelection)];
            details.push_back(ftxui::text("Patrón: " + selected.name));
            if (selected.valid()) {
                details.push_back(ftxui::text(
                    "Dimensiones: " + std::to_string(selected.height) + " × " +
                    std::to_string(selected.width) + "  ·  " +
                    std::to_string(selected.aliveCells) + " vivas"));
                if (!selected.type.empty()) {
                    details.push_back(ftxui::text("Tipo: " + selected.type));
                }
                if (selected.period) {
                    details.push_back(ftxui::text(
                        "Período: " + std::to_string(*selected.period)));
                }
                if (!selected.description.empty()) {
                    details.push_back(ftxui::text(selected.description) | ftxui::dim);
                }
            }
        } else {
            details.push_back(ftxui::text("Selecciona un archivo .txt para ver sus datos."));
        }

        Element detailContent = ftxui::vbox(std::move(details));
        if (!catalogPreview_.error.empty()) {
            detailContent = ftxui::vbox({
                std::move(detailContent),
                ftxui::text("Vista previa no disponible: " + catalogPreview_.error) |
                    ftxui::color(ftxui::Color::Red),
            });
        } else if (catalogPreview_.current && catalogPreview_.next) {
            const PatternEntry& selected = catalog_.entries[
                static_cast<std::size_t>(state_.patternSelection)];
            const std::size_t detailWidth = width < 64
                ? width : width - (width * 2 / 5) - 3;
            const bool horizontalPreviews = !compact && detailWidth >= 62;
            const std::size_t previewWidth = horizontalPreviews
                ? (detailWidth - 5) / 2
                : detailWidth > 4 ? detailWidth - 4 : 1;
            const std::size_t previewRows = horizontalPreviews
                ? (height > 18 ? std::max<std::size_t>(1, (height - 18) / 2) : 1)
                : (height > 22 ? std::max<std::size_t>(1, (height - 22) / 4) : 1);
            const std::size_t columns = std::max<std::size_t>(
                1, std::min(selected.width,
                    previewWidth > 4 ? (previewWidth - 4) / 3 : 1));
            const std::size_t rows = std::max<std::size_t>(
                1, std::min(selected.height, previewRows));
            const bool colors = colorsEnabled();
            const auto currentRows = boardRows(
                *catalogPreview_.current, rows, columns, colors);
            const auto nextRows = boardRows(
                *catalogPreview_.next, rows, columns, colors);
            const bool compactStack = !horizontalPreviews && height <= 28;
            Element current = compactStack
                ? ftxui::vbox({
                      heading("ACTUAL · generación 0"),
                      ftxui::vbox(currentRows),
                  })
                : panel("ACTUAL · generación 0", ftxui::vbox(currentRows));
            Element next = compactStack
                ? ftxui::vbox({
                      heading("SIGUIENTE · generación 1"),
                      ftxui::vbox(nextRows),
                  })
                : panel("SIGUIENTE · generación 1", ftxui::vbox(nextRows));
            Element previews = horizontalPreviews
                ? ftxui::hbox({std::move(current) | ftxui::flex,
                               ftxui::text(" "),
                               std::move(next) | ftxui::flex})
                : ftxui::vbox({std::move(current) | ftxui::flex,
                               std::move(next) | ftxui::flex});
            detailContent = ftxui::vbox({
                std::move(detailContent),
                ftxui::separator(),
                std::move(previews) | ftxui::flex,
            });
        }
        Element detailPanel = panel("DETALLE", std::move(detailContent));

        Element catalogPanels = width < 64
            ? ftxui::vbox({
                  std::move(patternList) | ftxui::flex,
                  std::move(detailPanel) | ftxui::flex,
              })
            : ftxui::hbox({
                  std::move(patternList) |
                      ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
                                  static_cast<int>((width * 2) / 5)),
                  ftxui::text(" "),
                  std::move(detailPanel) | ftxui::flex,
              });
        return ftxui::vbox({
            heading("CATÁLOGO DE PATRONES"),
            std::move(catalogPanels) | ftxui::flex,
        });
    }

    Element renderEngines() {
        return panel("MOTOR DE EJECUCIÓN",
            ftxui::vbox({
                ftxui::text("Motor actual: " + engineName(state_.engine)),
                engineMenu_->Render() | ftxui::flex,
            }));
    }

    Element renderThreads() {
        return panel("CONFIGURACIÓN OPENMP",
            ftxui::vbox({
                ftxui::text("Procesadores lógicos disponibles: " +
                            std::to_string(availableLogicalProcessors())),
                ftxui::text("Solicitados: " + std::to_string(state_.requestedThreads) +
                            "  ·  máximo permitido: " +
                            std::to_string(kMaxOpenMPThreads)),
                ftxui::text("El conteo lógico no representa núcleos físicos.") | ftxui::dim,
                threadMenu_->Render() | ftxui::flex,
            }));
    }

    Element renderInput(std::string title, const ftxui::Component& input, std::string hint) {
        return panel(std::move(title),
            ftxui::vbox({
                ftxui::text(std::move(hint)),
                input->Render() | ftxui::border,
                ftxui::text("Enter confirma  ·  Esc cancela"),
            }));
    }

    Element renderSimulation(std::size_t width, std::size_t height, bool compact) {
        if (!state_.simulation) {
            return panel("SIMULACIÓN", ftxui::text("No hay una simulación activa."));
        }
        const Game& game = state_.simulation->game();
        if (!preview_ || previewGeneration_ != game.generation() ||
            previewEngine_ != state_.engine) {
            preview_ = game;
            preview_->step();
            previewGeneration_ = game.generation();
            previewEngine_ = state_.engine;
            state_.effectiveThreads = state_.engine == Engine::OpenMP
                ? core::last_openmp_team_size() : 1;
        }
        const std::size_t rows = visibleBoardRows(height, game.height(), compact);
        const std::size_t panelWidth = compact
            ? std::max<std::size_t>(1, width > 8 ? width - 8 : 1)
            : std::max<std::size_t>(
                  1, (width - std::min<std::size_t>(width, 10)) / 2);
        const std::size_t columns = std::max<std::size_t>(
            1, std::min(game.width(), panelWidth / 4));
        const bool colors = colorsEnabled();
        Element current = panel("CURRENT · generación " +
            std::to_string(game.generation()),
            ftxui::vbox(boardRows(game, rows, columns, colors)));
        Element next = panel("NEXT · generación " +
            std::to_string(game.generation() + 1),
            ftxui::vbox(boardRows(*preview_, rows, columns, colors)));
        Element boards = compact
            ? ftxui::vbox({std::move(current) | ftxui::flex,
                           ftxui::text(""),
                           std::move(next) | ftxui::flex})
            : ftxui::hbox({std::move(current) | ftxui::flex,
                           ftxui::text(" "),
                           std::move(next) | ftxui::flex});
        std::string status = state_.simulation->isRunning() ? "En ejecución" : "Pausado";
        std::string threads = state_.engine == Engine::OpenMP
            ? "OpenMP · solicitados " + std::to_string(state_.requestedThreads) +
                " · efectivos " + (state_.effectiveThreads > 0
                    ? std::to_string(state_.effectiveThreads) : "pendientes")
            : "Secuencial";
        return ftxui::vbox({
            heading("SIMULACIÓN · " + state_.path),
            ftxui::text(std::to_string(game.width()) + " × " +
                        std::to_string(game.height()) + " · " + threads),
            std::move(boards) | ftxui::flex,
            ftxui::hbox({
                ftxui::text("Generación " + std::to_string(game.generation())),
                ftxui::text("  ·  " + std::to_string(game.aliveCount()) + " vivas"),
                ftxui::text("  ·  " + status) | ftxui::color(
                    state_.simulation->isRunning() ? ftxui::Color::Green :
                                                      ftxui::Color::Yellow),
            }),
        }) | ftxui::border;
    }

    Element renderBenchmark() {
        if (state_.benchmarkRunning) {
            return panel("BENCHMARK · Secuencial vs OpenMP",
                ftxui::vbox({
                    ftxui::text(activePattern(state_)),
                    ftxui::text("20 generaciones · 5 repeticiones · " +
                                std::to_string(state_.requestedThreads) +
                                " hilos OpenMP solicitados"),
                    ftxui::text("Ejecutando mediciones…") |
                        ftxui::color(ftxui::Color::Yellow),
                }));
        }
        if (!state_.benchmark) {
            return panel("BENCHMARK",
                ftxui::text("No hay resultados. Pulsa Esc para volver."));
        }
        std::vector<Element> reportRows;
        std::istringstream report(benchmarkReport(*state_.benchmark));
        std::string line;
        while (std::getline(report, line)) {
            reportRows.push_back(ftxui::text(std::move(line)));
        }
        if (state_.input && state_.input->cells.size() < 256) {
            reportRows.push_back(
                ftxui::text("Nota: tablero pequeño; el overhead OpenMP puede dominar.") |
                ftxui::color(ftxui::Color::Yellow));
        }
        reportRows.push_back(
            ftxui::text("Pulsa Enter o Esc para volver.") | ftxui::dim);
        return panel("BENCHMARK · " + activePattern(state_),
            ftxui::vbox(std::move(reportRows)));
    }

    // Render del modo didactico. Esta vista NO recalcula reglas de
    // Conway; solo muestra la StepTrace capturada por el worker y
    // la revelacion progresiva basada en el reloj de reproduccion.
    Element renderDidactic(std::size_t width, std::size_t height, bool compact) {
        if (!state_.didactic) {
            return panel("VISUALIZACIÓN DIDÁCTICA",
                ftxui::text("No hay un controlador activo."));
        }
        const DidacticFrame frame = state_.didactic->snapshot();

        if (frame.is_preparing) {
            return panel("VISUALIZACIÓN DIDÁCTICA",
                ftxui::vbox({
                    ftxui::text("Preparando generación instrumentada...") |
                        ftxui::color(ftxui::Color::Yellow),
                    ftxui::text("La UI permanece receptiva."),
                }) | ftxui::flex);
        }
        if (!frame.has_data) {
            return panel("VISUALIZACIÓN DIDÁCTICA",
                ftxui::text("Sin datos. Pulsa R para reiniciar."));
        }
        if (!frame.error_message.empty() && frame.phase == DidacticController::Phase::Error) {
            return panel("VISUALIZACIÓN DIDÁCTICA",
                ftxui::vbox({
                    ftxui::text("Error en la generación instrumentada."),
                    ftxui::text(frame.error_message),
                }));
        }

        const std::size_t rows = visibleBoardRows(height, frame.height, compact);
        const std::size_t panelWidth = compact
            ? std::max<std::size_t>(1, width > 8 ? width - 8 : 1)
            : std::max<std::size_t>(
                  1, (width - std::min<std::size_t>(width, 10)) / 2);
        const std::size_t columns = std::max<std::size_t>(
            1, std::min(frame.width, panelWidth / 4));
        const bool colors = colorsEnabled();

        Element currentPanel = panel(
            "CURRENT · generación " + std::to_string(frame.current_generation),
            ftxui::vbox(didacticCurrentRows(frame, rows, columns, colors)));

        // Panel NEXT con indicador de particion encima.
        std::vector<Element> nextWithIndicator;
        nextWithIndicator.push_back(partitionIndicator(frame));
        nextWithIndicator.push_back(ftxui::text(" "));
        for (auto& e : didacticBoardRows(frame, rows, columns, colors)) {
            nextWithIndicator.push_back(std::move(e));
        }
        Element nextPanel = panel(
            "NEXT · generación " + std::to_string(frame.next_generation) +
                (frame.fully_revealed
                    ? "  ·  REPRODUCCIÓN COMPLETA"
                    : (frame.playing ? "  ·  REPRODUCIENDO" : "  ·  EN PAUSA")),
            ftxui::vbox(std::move(nextWithIndicator)));

        Element boards = compact
            ? ftxui::vbox({std::move(currentPanel) | ftxui::flex,
                           ftxui::text(""),
                           std::move(nextPanel) | ftxui::flex})
            : ftxui::hbox({std::move(currentPanel) | ftxui::flex,
                           ftxui::text(" "),
                           std::move(nextPanel) | ftxui::flex});

        Element workersPanel = renderDidacticWorkers(frame, colors);

        std::string statusLine = "Motor: " + frame.engine_name +
            "  ·  Generación actual " +
            std::to_string(frame.current_generation) + " → siguiente " +
            std::to_string(frame.next_generation);
        if (frame.engine_name == "OpenMP") {
            statusLine += "  ·  hilos solicitados " +
                std::to_string(frame.requested_threads) +
                "  ·  efectivos " + std::to_string(frame.effective_threads);
        }
        statusLine += "  ·  procesadores lógicos " +
            std::to_string(frame.logical_processors);

        const std::string warning =
            "REPRODUCCIÓN DIDÁCTICA · No es ejecución en vivo";
        std::string speedLine = "Velocidad: " + speedLabel(frame.speed) +
            "  ·  [" + (frame.playing ? std::string("Reproduciendo")
                                       : std::string("En pausa")) + "]";
        if (frame.fully_revealed) {
            speedLine += "  ·  pulse N para consolidar la siguiente generación";
        } else if (!frame.playing) {
            speedLine += "  ·  pulse Espacio para reproducir";
        }

        return ftxui::vbox({
            ftxui::text(warning) | ftxui::bold | ftxui::color(ftxui::Color::Cyan),
            heading("MODO DIDÁCTICO · " + state_.path),
            ftxui::text(statusLine) | ftxui::dim,
            ftxui::text(" "),
            std::move(boards) | ftxui::flex,
            ftxui::text(" "),
            std::move(workersPanel) | ftxui::flex,
            ftxui::text(speedLine) | ftxui::dim,
        }) | ftxui::border;
    }

    // Indicador de particion horizontal sobre el panel NEXT. Una
    // barra pequena (un caracter por fila logica) con el color del
    // worker responsable. Se renderiza en una sola linea usando
    // ftxui::hbox.
    Element partitionIndicator(const DidacticFrame& frame) {
        if (frame.workers.empty() || frame.height == 0) {
            return ftxui::text("");
        }
        std::vector<Element> segments;
        for (const auto& w : frame.workers) {
            const int span = w.end_row - w.first_row;
            if (span <= 0) continue;
            const std::size_t chars = std::max<std::size_t>(1,
                static_cast<std::size_t>(span));
            // U+2580 '▀' (Upper Half Block) en UTF-8.
            const std::string block(chars * 3, '\0');
            std::string filled;
            filled.reserve(chars * 3);
            for (std::size_t k = 0; k < chars; ++k) {
                filled += "\xE2\x96\x80";
            }
            segments.push_back(ftxui::text(filled) |
                ftxui::bgcolor(workerColor(w.color_index)));
            (void)block;
        }
        return ftxui::hbox(std::move(segments));
    }

    // Panel inferior: lista de trabajadores con su id, filas,
    // celdas, duracion medida y barra de progreso.
    Element renderDidacticWorkers(const DidacticFrame& frame, bool colors) {
        std::vector<Element> rows;
        rows.push_back(ftxui::text("TRABAJO POR HILO") | ftxui::bold);
        if (frame.workers.empty()) {
            rows.push_back(ftxui::text("Sin trabajadores registrados.") | ftxui::dim);
            return ftxui::vbox(std::move(rows));
        }
        // Encabezado
        rows.push_back(ftxui::hbox({
            ftxui::text("ID") | ftxui::bold,
            ftxui::text("  "),
            ftxui::text("Filas") | ftxui::bold,
            ftxui::text("          "),
            ftxui::text("Celdas") | ftxui::bold,
            ftxui::text("   "),
            ftxui::text("Duración") | ftxui::bold,
            ftxui::text("    "),
            ftxui::text("Progreso") | ftxui::bold,
        }) | ftxui::dim);
        // Limite la cantidad de filas mostradas para no romper el
        // panel en terminales pequeñas.
        const std::size_t max_show = 8;
        const std::size_t show = std::min(max_show, frame.workers.size());
        for (std::size_t i = 0; i < show; ++i) {
            const auto& w = frame.workers[i];
            const std::string tlabel = "T" + std::to_string(w.thread_id);
            const std::string rows_label = "[" +
                std::to_string(w.first_row) + ", " +
                std::to_string(w.end_row) + ")";
            const std::string cells_label = std::to_string(w.cells_processed);
            const double dur_ms =
                static_cast<double>(w.loop_duration_ns) / 1.0e6;
            char dur_buf[32];
            std::snprintf(dur_buf, sizeof(dur_buf), "%.3f ms", dur_ms);
            const std::string dur_label = dur_buf;
            const int bar_chars = 20;
            const int filled = std::max(0,
                std::min(bar_chars,
                    static_cast<int>(w.progress * bar_chars + 0.5)));
            // Construimos la barra con dos bloques UTF-8: '·' (punto
            // medio) para vacio y '█' (bloque completo) para lleno.
            std::string bar;
            bar.reserve(bar_chars * 3);
            for (int j = 0; j < filled; ++j) {
                bar += "\xE2\x96\x88";  // '█'
            }
            for (int j = filled; j < bar_chars; ++j) {
                bar += "\xC2\xB7";  // '·'
            }
            const std::string status = w.is_sequential
                ? std::string("T0 único")
                : (w.progress >= 1.0
                    ? std::string("completo")
                    : (frame.playing
                        ? std::string("activo")
                        : std::string("pendiente")));
            Element bar_el = colors
                ? ftxui::text(bar) | ftxui::color(workerColor(w.color_index))
                : ftxui::text(bar) | ftxui::dim;
            rows.push_back(ftxui::hbox({
                ftxui::text(tlabel) | ftxui::bold,
                ftxui::text("  "),
                ftxui::text(rows_label),
                ftxui::text("  "),
                ftxui::text(cells_label),
                ftxui::text("   "),
                ftxui::text(dur_label),
                ftxui::text("    "),
                std::move(bar_el),
                ftxui::text("  "),
                ftxui::text(status) | ftxui::dim,
            }));
        }
        if (frame.workers.size() > show) {
            rows.push_back(ftxui::text(
                "Mostrando " + std::to_string(show) + " de " +
                std::to_string(frame.workers.size()) +
                " trabajadores.") | ftxui::dim);
        }
        return ftxui::vbox(std::move(rows));
    }

    std::string speedLabel(double s) const {
        if (std::abs(s - 0.25) < 1e-9) return "0.25x";
        if (std::abs(s - 0.5) < 1e-9) return "0.5x";
        if (std::abs(s - 1.0) < 1e-9) return "1.0x";
        if (std::abs(s - 2.0) < 1e-9) return "2.0x";
        if (std::abs(s - 4.0) < 1e-9) return "4.0x";
        if (std::abs(s - 8.0) < 1e-9) return "8.0x";
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << s << "x";
        return out.str();
    }

    std::string footerText() const {
        switch (state_.page) {
            case Page::Main: return "↑↓ navegar · Enter seleccionar · C patrones · 0 salir";
            case Page::Patterns: return "↑↓ navegar · Enter cargar · M ruta manual · Esc volver";
            case Page::Engines: return "↑↓ navegar · Enter seleccionar · S/O motor · Esc volver";
            case Page::Threads: return "↑↓ navegar · Enter seleccionar · Esc volver";
            case Page::PathInput:
            case Page::ThreadInput: return "Enter confirmar · Esc cancelar";
            case Page::Simulation: return "N paso · Espacio/P pausa/reanuda · R reiniciar · Esc menú";
            case Page::Benchmark: return "Espacio/Esc volver · resultados comparan estados equivalentes";
            case Page::Didactic: return "Espacio reproducir · N siguiente · R reiniciar · + más rápido · - más lento · Esc menú";
        }
        return {};
    }

    ftxui::ScreenInteractive& screen_;
    DifferentialOutputBuffer& output_;
    InteractiveState state_;
    std::vector<std::string> mainItems_;
    std::vector<std::string> patternItems_;
    std::vector<std::string> patternActionItems_;
    std::vector<std::string> engineItems_;
    std::vector<std::string> threadItems_;
    PatternCatalog catalog_;
    PatternPreview catalogPreview_;
    Page pathReturnPage_{Page::Main};
    Page destinationAfterPath_{Page::Main};
    std::optional<Game> preview_;
    std::size_t previewGeneration_{static_cast<std::size_t>(-1)};
    Engine previewEngine_{Engine::Sequential};
    std::atomic<unsigned> pendingEvents_{0};
    std::jthread playbackTimer_;
    std::mutex benchmarkMutex_;
    std::optional<BenchmarkResult> benchmarkResult_;
    std::string benchmarkError_;
    std::thread benchmarkWorker_;
    ftxui::Component mainMenu_;
    ftxui::Component patternMenu_;
    ftxui::Component patternActionsMenu_;
    ftxui::Component engineMenu_;
    ftxui::Component threadMenu_;
    ftxui::Component pathInput_;
    ftxui::Component threadInput_;
    ftxui::Component simulationView_ = ftxui::Renderer([] { return ftxui::text(""); });
    ftxui::Component benchmarkView_ = ftxui::Renderer([] { return ftxui::text(""); });
    ftxui::Component didacticView_ = ftxui::Renderer([] { return ftxui::text(""); });
};

} // namespace

int runFtxuiApp() {
    std::streambuf* original = std::cout.rdbuf();
    DifferentialOutputBuffer differentialOutput(original);
    std::cout.rdbuf(&differentialOutput);
    try {
        auto screen = ftxui::ScreenInteractive::Fullscreen();
        auto app = ftxui::Make<InteractiveApp>(screen, differentialOutput);
        screen.Loop(app);
    } catch (...) {
        std::cout.rdbuf(original);
        throw;
    }
    std::cout.rdbuf(original);
    return 0;
}

} // namespace conway::app

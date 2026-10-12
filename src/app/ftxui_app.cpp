#include "app/ftxui_app.hpp"

#include "app/benchmark.hpp"
#include "app/controls.hpp"
#include "app/pattern_catalog.hpp"
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

enum class Page {
    Main,
    Patterns,
    Engines,
    Threads,
    PathInput,
    ThreadInput,
    Simulation,
    Benchmark
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
    int engineSelection{0};
    int threadSelection{0};
    std::string pathDraft;
    std::string threadDraft;
    std::unique_ptr<SimulationSession> simulation;
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
            output += "\x1b[0m\x1b[K";
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
    return ftxui::text("  ") |
        ftxui::bgcolor(alive ? ftxui::Color::Green : ftxui::Color::GrayDark);
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
    output.push_back(ftxui::text(
        gridBorder(visibleColumns, "┬", "┌", "┐")));
    for (std::size_t row = 0; row < visibleRows; ++row) {
        std::vector<Element> cells;
        cells.reserve(visibleColumns * 2 + 1);
        cells.push_back(ftxui::text("│"));
        for (std::size_t column = 0; column < visibleColumns; ++column) {
            cells.push_back(cellElement(game.isAlive(row, column), colors));
            cells.push_back(ftxui::text("│"));
        }
        output.push_back(ftxui::hbox(std::move(cells)));
        if (row + 1 < visibleRows) {
            output.push_back(ftxui::text(
                gridBorder(visibleColumns, "┼", "├", "┤")));
        } else {
            output.push_back(ftxui::text(
                gridBorder(visibleColumns, "┴", "└", "┘")));
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
                                 [this] { selectPattern(); })),
          engineMenu_(makeMenu(engineItems_, state_.engineSelection,
                               [this] { selectEngine(); })),
          threadMenu_(makeMenu(threadItems_, state_.threadSelection,
                               [this] { selectThreadOption(); })),
          pathInput_(makePathInput()),
          threadInput_(makeThreadInput()) {
        omp_set_num_threads(state_.requestedThreads);
        Add(mainMenu_);
        Add(patternMenu_);
        Add(engineMenu_);
        Add(threadMenu_);
        Add(pathInput_);
        Add(threadInput_);
        Add(simulationView_);
        Add(benchmarkView_);
        mainItems_ = {
            "Benchmark",
            "Visualización",
            "Seleccionar motor",
            "Configurar hilos OpenMP",
            "Catálogo de patrones",
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
                content = renderPatterns();
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

    bool OnEvent(Event event) override {
        if (event == Event::Custom) {
            const unsigned pending = pendingEvents_.exchange(0, std::memory_order_acq_rel);
            if ((pending & kBenchmarkEvent) != 0) {
                receiveBenchmarkResult();
            }
            if ((pending & kPlaybackEvent) != 0 && state_.page == Page::Simulation &&
                state_.simulation && state_.simulation->isRunning()) {
                state_.simulation->advanceIfRunning();
                state_.effectiveThreads = state_.engine == Engine::OpenMP
                    ? core::last_openmp_team_size() : 1;
                schedulePlaybackTick();
            }
            return true;
        }
        if (event == Event::Escape) {
            goBack();
            return true;
        }
        if (isMenuPage(state_.page)) {
            if (event == Event::ArrowUp) {
                moveMenuSelection(-1);
                return true;
            }
            if (event == Event::ArrowDown) {
                moveMenuSelection(1);
                return true;
            }
            if (event == Event::Return) {
                selectMenuItem();
                return true;
            }
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

    static bool isMenuPage(Page page) {
        return page == Page::Main || page == Page::Patterns ||
            page == Page::Engines || page == Page::Threads;
    }

    int& menuSelection() {
        switch (state_.page) {
            case Page::Main: return state_.mainSelection;
            case Page::Patterns: return state_.patternSelection;
            case Page::Engines: return state_.engineSelection;
            case Page::Threads: return state_.threadSelection;
            case Page::PathInput:
            case Page::ThreadInput:
            case Page::Simulation:
            case Page::Benchmark:
                break;
        }
        return state_.mainSelection;
    }

    std::size_t menuSize() const {
        switch (state_.page) {
            case Page::Main: return mainItems_.size();
            case Page::Patterns: return patternItems_.size();
            case Page::Engines: return engineItems_.size();
            case Page::Threads: return threadItems_.size();
            case Page::PathInput:
            case Page::ThreadInput:
            case Page::Simulation:
            case Page::Benchmark:
                break;
        }
        return 0;
    }

    void moveMenuSelection(int direction) {
        const std::size_t size = menuSize();
        if (size == 0) {
            return;
        }
        int& selection = menuSelection();
        const auto current = static_cast<std::size_t>(std::max(0, selection));
        const std::size_t next = direction < 0
            ? (current + size - 1) % size
            : (current + 1) % size;
        selection = static_cast<int>(next);
    }

    void selectMenuItem() {
        switch (state_.page) {
            case Page::Main: selectMain(); break;
            case Page::Patterns: selectPattern(); break;
            case Page::Engines: selectEngine(); break;
            case Page::Threads: selectThreadOption(); break;
            case Page::PathInput:
            case Page::ThreadInput:
            case Page::Simulation:
            case Page::Benchmark:
                break;
        }
    }

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
        std::function<void()> onEnter
    ) {
        ftxui::MenuOption option;
        option.entries_option.transform = [](const ftxui::EntryState& state) {
            Element entry = ftxui::text(
                std::string(state.active ? "> " : "  ") + state.label);
            if (state.active) {
                entry = entry | ftxui::bold;
                if (colorsEnabled()) {
                    entry = entry | ftxui::color(ftxui::Color::Cyan);
                }
            }
            if (state.focused) {
                entry = entry | ftxui::inverted;
            }
            return entry;
        };
        option.on_enter = std::move(onEnter);
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
            case Page::Patterns: return patternMenu_;
            case Page::Engines: return engineMenu_;
            case Page::Threads: return threadMenu_;
            case Page::PathInput: return pathInput_;
            case Page::ThreadInput: return threadInput_;
            case Page::Simulation: return simulationView_;
            case Page::Benchmark: return benchmarkView_;
        }
        return mainMenu_;
    }

    void refreshPatterns() {
        catalog_ = discoverPatterns("patterns");
        patternItems_.clear();
        for (const PatternEntry& entry : catalog_.entries) {
            std::string label = entry.name;
            if (entry.valid()) {
                label += "  ·  " + std::to_string(entry.height) + " × " +
                    std::to_string(entry.width) + "  ·  " + entry.description;
            } else {
                label += "  ·  inválido: " + entry.error;
            }
            patternItems_.push_back(std::move(label));
        }
        if (patternItems_.empty()) {
            state_.patternSelection = 0;
        } else if (state_.input) {
            for (std::size_t i = 0; i < catalog_.entries.size(); ++i) {
                if (catalog_.entries[i].path == state_.path) {
                    state_.patternSelection = static_cast<int>(i);
                    break;
                }
            }
        }
        manualPatternIndex_ = static_cast<int>(patternItems_.size());
        patternItems_.push_back("Introducir una ruta manual...");
        patternItems_.push_back("Volver");
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
            case 5: screen_.Exit(); break;
            default: break;
        }
    }

    void selectPattern() {
        if (state_.patternSelection == manualPatternIndex_) {
            state_.pathDraft.clear();
            pathReturnPage_ = Page::Patterns;
            destinationAfterPath_ = state_.returnAfterCatalog;
            state_.page = Page::PathInput;
            return;
        }
        if (state_.patternSelection >= static_cast<int>(catalog_.entries.size())) {
            state_.page = state_.returnAfterCatalog;
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
            }
        }
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

    Element renderPatterns() {
        if (!catalog_.error.empty()) {
            return panel("CATÁLOGO DE PATRONES",
                         ftxui::text("Error: " + catalog_.error));
        }
        return panel("CATÁLOGO DE PATRONES",
            ftxui::vbox({
                ftxui::text("Selecciona un patrón; los archivos .txt se descubren al abrir esta vista."),
                patternMenu_->Render() | ftxui::flex,
                ftxui::text("Entrada activa: " + activePattern(state_)) | ftxui::dim,
            }));
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
                           std::move(next) | ftxui::flex})
            : ftxui::hbox({std::move(current) | ftxui::flex,
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

    std::string footerText() const {
        switch (state_.page) {
            case Page::Main: return "↑↓ navegar · Enter seleccionar · C patrones · 0 salir";
            case Page::Patterns: return "↑↓ navegar · Enter cargar · M ruta manual · Esc volver";
            case Page::Engines: return "↑↓ navegar · Enter seleccionar · S/O motor · Esc volver";
            case Page::Threads: return "↑↓ navegar · Enter seleccionar · Esc volver";
            case Page::PathInput:
            case Page::ThreadInput: return "Enter confirmar · Esc cancelar";
            case Page::Simulation: return "N paso · Espacio/P pausa/reanuda · R reiniciar · Esc menú";
            case Page::Benchmark: return "Esc volver · resultados comparan estados equivalentes";
        }
        return {};
    }

    ftxui::ScreenInteractive& screen_;
    DifferentialOutputBuffer& output_;
    InteractiveState state_;
    std::vector<std::string> mainItems_;
    std::vector<std::string> patternItems_;
    std::vector<std::string> engineItems_;
    std::vector<std::string> threadItems_;
    PatternCatalog catalog_;
    int manualPatternIndex_{0};
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
    ftxui::Component engineMenu_;
    ftxui::Component threadMenu_;
    ftxui::Component pathInput_;
    ftxui::Component threadInput_;
    ftxui::Component simulationView_ = ftxui::Renderer([] { return ftxui::text(""); });
    ftxui::Component benchmarkView_ = ftxui::Renderer([] { return ftxui::text(""); });
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

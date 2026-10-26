#include "app/ftxui_app.hpp"

#include "app/benchmark.hpp"
#include "app/experiment_form_view.hpp"
#include "app/experiment_execution.hpp"
#include "app/comparison_view.hpp"
#include "app/scaling_view.hpp"
#include "ui/responsive_layout.hpp"
#include "ui/allocated_element.hpp"
#include "experiment_input_detail.hpp"
#include "app/controls.hpp"
#include "app/didactic_session.hpp"
#include "app/mpi_didactic_view.hpp"
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
#include <ftxui/screen/string.hpp>

#include <omp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
Element cellElement(bool alive, std::size_t cell_chars, bool colors);
Element didacticCellElement(bool alive, bool revealed,
                             std::int8_t worker_index, bool colors);
Element didacticCellElement(bool alive, bool revealed,
                             std::int8_t worker_index,
                             std::size_t cell_chars, bool colors);
Element gridElement(std::string value, bool colors);
std::string gridBorder(
    std::size_t columns,
    std::string_view junction,
    std::string_view left,
    std::string_view right);
std::string gridBorder(
    std::size_t columns,
    std::string_view junction,
    std::string_view left,
    std::string_view right,
    std::size_t cell_chars);
std::size_t visibleBoardRows(
    std::size_t screenHeight,
    std::size_t boardHeight,
    bool compact);
std::vector<Element> boardRows(
    const conway::Game& game,
    std::size_t rows,
    std::size_t columns,
    bool colors, std::size_t rowOffset = 0, std::size_t columnOffset = 0, bool showViewport = true);
ftxui::Color workerColor(int index);
class DifferentialOutputBuffer;

// Geometria adaptable para los tableros de la visualizacion didactica.
// Mantiene el tablero cuadrado (en coordenadas logicas) y elige el
// tamano de celda mas grande que permita mostrarlo completo dentro
// del area disponible. Si no cabe, usa cell_chars = 2 (el menor) y un
// viewport con el mayor numero posible de filas y columnas.
struct DidacticBoardGeometry {
    std::size_t cell_chars = 2;
    std::size_t visible_rows = 0;
    std::size_t visible_cols = 0;
    std::size_t row_offset = 0;
    std::size_t col_offset = 0;
    std::size_t board_chars = 0;
    std::size_t board_lines = 0;
    bool full_board_shown = false;
};

DidacticBoardGeometry computeDidacticBoardGeometry(
    std::size_t panel_inner_width,
    std::size_t panel_inner_height,
    std::size_t board_width,
    std::size_t board_height);

Element renderDidacticBoardPanel(
    const conway::app::DidacticFrame& frame,
    const DidacticBoardGeometry& geom,
    bool is_next,
    bool colors, bool showViewport = true);

enum class Page {
    Main,
    Patterns,
    Engines,
    Threads,
    PathInput,
    ThreadInput,
    MPIInput,
    Simulation,
    ExperimentExecution,
    ComparisonConfig,
    BenchmarkMenu,
    BenchmarkScaling,
    BenchmarkResults,
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
    int mpiProcesses{1};
    std::string mpiDraft;
    Page page{Page::Main};
    Page returnAfterCatalog{Page::Main};
    int mainSelection{0};
    int benchmarkSelection{0};
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
    return engine == Engine::Sequential ? "Secuencial" : engine == Engine::OpenMP ? "OpenMP" : "MPI";
}

std::string activePattern(const InteractiveState& state) {
    if (!state.input) {
        return "Sin patrón cargado";
    }
    return state.path + "  ·  " + std::to_string(state.input->height) + " × " +
        std::to_string(state.input->width);
}

// Identidad compartida; no depende de la paleta ANSI del terminal.
ftxui::Color identityCyan() {
    return ftxui::Color::RGB(83, 191, 208);  // #53BFD0
}

Element heading(std::string label) {
    Element output = ftxui::text(std::move(label)) | ftxui::bold;
    return colorsEnabled() ? output | ftxui::color(identityCyan()) : output;
}

// Tres filas de 60 columnas: suficiente presencia sin ocupar un banner grande.
Element renderAppTitle(bool expanded) {
    if (!expanded) return heading("CONWAY PARALLEL") | ftxui::hcenter;
    static constexpr std::array<std::string_view, 3> lettering{
        "███ ███ █  █ █   █ ███ █ █   ███ ███ ███ ███ █   █   ███ █  ",
        "█   █ █ ██ █ █ █ █ █▄█  █    ██▀ █▄█ ██▄ █▄█ █   █   ██  █  ",
        "███ ███ █ ██ ██ ██ █ █  █    █   █ █ █ █ █ █ ███ ███ ███ ███",
    };
    ftxui::Elements lines;
    for (const auto row : lettering) lines.push_back(heading(std::string(row)));
    return ftxui::vbox(std::move(lines)) | ftxui::hcenter;
}

// Identidad compartida, sin datos de configuracion dentro del recuadro.
Element renderAppHeader(std::size_t width, std::size_t height) {
    const bool expandedTitle = width >= 120 && height >= 40;
    ftxui::Elements lines{renderAppTitle(expandedTitle)};
    if (width >= 64 && height > 28) {
        lines.push_back(ftxui::text("Simulación y análisis de programación paralela") |
                        ftxui::hcenter | ftxui::dim);
    }
    return ftxui::vbox(std::move(lines));
}

// Abreviar solo el nombre conserva las dimensiones y la configuracion visibles.
std::string fitContextLabel(const std::string& label, std::size_t columns) {
    if (ftxui::string_width(label) <= static_cast<int>(columns)) return label;
    if (columns == 0) return {};
    std::string result;
    std::size_t used = 0;
    for (const auto& glyph : ftxui::Utf8ToGlyphs(label)) {
        const auto glyphWidth = static_cast<std::size_t>(ftxui::string_width(glyph));
        if (used + glyphWidth >= columns) break;
        result += glyph;
        used += glyphWidth;
    }
    return result + "…";
}

// Una barra secundaria, derivada del estado compartido en cada render.
Element renderContextBar(const InteractiveState& state, std::size_t width) {
    const bool compact = width < 64;
    std::string label = compact ? "" : "Patrón: ";
    std::string dimensions;
    if (state.input) {
        label += std::filesystem::path(state.path).filename().string();
        dimensions = std::to_string(state.input->height) + "×" +
            std::to_string(state.input->width);
        dimensions = compact ? " " + dimensions : " (" + dimensions + ")";
    } else {
        label += "Sin cargar";
    }
    const std::string engine = width < 44
        ? (state.engine == Engine::Sequential ? "Sec." : state.engine == Engine::OpenMP ? "OMP" : "MPI")
        : engineName(state.engine);
    const bool experiment = state.page == Page::ComparisonConfig || state.page == Page::BenchmarkScaling ||
        state.page == Page::ExperimentExecution || state.page == Page::BenchmarkResults || state.page == Page::BenchmarkMenu;
    std::string suffix = dimensions + " · " + (experiment ? "Simulación: " : (compact ? "" : "Motor: ")) + engine;
    if (state.engine == Engine::OpenMP) {
        suffix += width < 44 ? " · H:" : compact ? " · Hilos: " : " · Hilos solicitados: ";
        suffix += std::to_string(state.requestedThreads);
    }
    if (state.engine == Engine::MPI) suffix += " · P:" + std::to_string(state.mpiProcesses);
    const auto suffixWidth = static_cast<std::size_t>(ftxui::string_width(suffix));
    const auto labelWidth = width > suffixWidth ? width - suffixWidth : 0;
    return ftxui::text(fitContextLabel(label, labelWidth) + suffix) | ftxui::dim;
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

// Variante con ancho de celda variable. En modo color se usa
// `cell_chars` espacios como fondo; en modo monocromo se conservan
// los bloques de 2 chars para no romper la estetica de la
// visualizacion normal.
Element cellElement(bool alive, std::size_t cell_chars, bool colors) {
    if (cell_chars < 2) cell_chars = 2;
    if (cell_chars > 8) cell_chars = 8;
    if (!colors) {
        return ftxui::text(alive ? "▓▓" : "░░");
    }
    const auto background = alive
        ? ftxui::Color::RGB(98, 232, 137)
        : ftxui::Color::RGB(38, 49, 56);
    return ftxui::text(std::string(cell_chars, ' ')) | ftxui::bgcolor(background);
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

// Variante con ancho de celda variable. Mismas reglas de color
// que la version de 2 chars, pero el fondo ocupa `cell_chars`.
Element didacticCellElement(bool alive, bool revealed,
                             std::int8_t worker_index,
                             std::size_t cell_chars, bool colors) {
    if (cell_chars < 2) cell_chars = 2;
    if (cell_chars > 8) cell_chars = 8;
    if (!revealed) {
        return ftxui::text(std::string(cell_chars, ' ')) |
            ftxui::bgcolor(ftxui::Color::RGB(0x1F, 0x2A, 0x30));
    }
    if (!colors) {
        return ftxui::text(alive ? "▓▓" : "░░");
    }
    if (alive) {
        return ftxui::text(std::string(cell_chars, ' ')) |
            ftxui::bgcolor(ftxui::Color::RGB(98, 232, 137));
    }
    return ftxui::text(std::string(cell_chars, ' ')) |
        ftxui::bgcolor(workerColor(worker_index));
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

// Variante con ancho de celda variable: cada celda del borde se
// rellena con `cell_chars` copias del caracter "─" (U+2580 top half
// block no sirve: usamos la barra horizontal UTF-8 E2 94 80).
std::string gridBorder(
    std::size_t columns,
    std::string_view junction,
    std::string_view left,
    std::string_view right,
    std::size_t cell_chars
) {
    if (cell_chars < 2) cell_chars = 2;
    if (cell_chars > 8) cell_chars = 8;
    static const std::string kDash = "\xE2\x94\x80";  // "─" UTF-8
    std::string dash;
    dash.reserve(cell_chars * kDash.size());
    for (std::size_t i = 0; i < cell_chars; ++i) {
        dash += kDash;
    }
    std::string output(left);
    for (std::size_t column = 0; column < columns; ++column) {
        output += dash;
        if (column + 1 < columns) {
            output += junction;
        }
    }
    output += right;
    return output;
}

// Calculo de la geometria adaptable. Si el tablero completo cabe en
// el area disponible con cell_chars >= 2, se elige el cell_chars
// mas grande posible. Si no cabe, se usa cell_chars = 2 (el menor)
// y un viewport con el mayor numero de filas/columnas que quepan.
DidacticBoardGeometry computeDidacticBoardGeometry(
    std::size_t panel_inner_width,
    std::size_t panel_inner_height,
    std::size_t board_width,
    std::size_t board_height
) {
    DidacticBoardGeometry g;
    if (panel_inner_width < 3 || panel_inner_height < 3 ||
        board_width == 0 || board_height == 0) {
        return g;
    }
    // Intentar con cell_chars de mayor a menor hasta que el tablero
    // completo quepa.
    for (std::size_t cw = 8; cw >= 2; --cw) {
        const std::size_t needed_w = board_width * (cw + 1) + 1;
        const std::size_t needed_h = board_height * 2 + 1;
        if (needed_w <= panel_inner_width &&
            needed_h <= panel_inner_height) {
            g.cell_chars = cw;
            g.visible_rows = board_height;
            g.visible_cols = board_width;
            g.board_chars = needed_w;
            g.board_lines = needed_h;
            g.full_board_shown = true;
            return g;
        }
    }
    // No cabe el tablero completo. Usar cell_chars = 2 (maxima cantidad
    // de columnas visibles) y calcular el viewport mas grande que
    // quepa en el area.
    g.cell_chars = 2;
    g.visible_cols = (panel_inner_width >= 1)
        ? std::min(board_width, (panel_inner_width - 1) / 3)
        : 0;
    g.visible_rows = (panel_inner_height >= 1)
        ? std::min(board_height, (panel_inner_height - 1) / 2)
        : 0;
    g.board_chars = g.visible_cols * 3 + 1;
    g.board_lines = g.visible_rows * 2 + 1;
    return g;
}

// Renderiza un panel (CURRENT o NEXT) aplicando la geometria. La
// cuadrícula se centra horizontal y verticalmente dentro del area
// disponible usando ftxui::filler. Si la geometria indica viewport
// parcial, se anade la nota "Viewport: r/h filas, c/w columnas".
Element renderDidacticBoardPanel(
    const conway::app::DidacticFrame& frame,
    const DidacticBoardGeometry& geom,
    bool is_next,
    bool colors, bool showViewport
) {
    if (geom.visible_rows == 0 || geom.visible_cols == 0 ||
        frame.width == 0 || frame.height == 0) {
        return ftxui::text("Tablero sin espacio") | ftxui::dim;
    }

    const std::size_t cell = geom.cell_chars;
    std::vector<Element> rows;
    rows.reserve(geom.visible_rows * 2 + 3);

    rows.push_back(gridElement(
        gridBorder(geom.visible_cols, "┬", "┌", "┐", cell), colors));

    for (std::size_t r = 0; r < geom.visible_rows; ++r) {
        const std::size_t logical_r = r + geom.row_offset;
        std::vector<Element> cells;
        cells.reserve(geom.visible_cols * 2 + 1);
        cells.push_back(gridElement("│", colors));
        for (std::size_t c = 0; c < geom.visible_cols; ++c) {
            const std::size_t logical_c = c + geom.col_offset;
            if (logical_r >= frame.height || logical_c >= frame.width) {
                cells.push_back(cellElement(false, cell, colors));
            } else {
                const std::size_t idx = logical_r * frame.width + logical_c;
                const bool alive = is_next
                    ? (frame.next_alive_map[idx] != 0)
                    : (frame.current_alive_map[idx] != 0);
                if (is_next) {
                    const bool revealed = frame.revealed[idx] != 0;
                    const std::int8_t wid = revealed
                        ? frame.worker_id[idx] : std::int8_t{-1};
                    cells.push_back(didacticCellElement(
                        alive, revealed, wid, cell, colors));
                } else {
                    cells.push_back(cellElement(alive, cell, colors));
                }
            }
            cells.push_back(gridElement("│", colors));
        }
        rows.push_back(ftxui::hbox(std::move(cells)));
        if (r + 1 < geom.visible_rows) {
            rows.push_back(gridElement(
                gridBorder(geom.visible_cols, "┼", "├", "┤", cell), colors));
        } else {
            rows.push_back(gridElement(
                gridBorder(geom.visible_cols, "┴", "└", "┘", cell), colors));
        }
    }

    if (showViewport && !geom.full_board_shown) {
        rows.push_back(ftxui::text(
            "Viewport: filas " + std::to_string(geom.row_offset + 1) + "–" +
            std::to_string(geom.row_offset + geom.visible_rows) + "/" + std::to_string(frame.height) +
            " · col. " + std::to_string(geom.col_offset + 1) + "–" +
            std::to_string(geom.col_offset + geom.visible_cols) + "/" + std::to_string(frame.width)) | ftxui::dim);
    }

    Element board = ftxui::vbox(std::move(rows));
    // Centrar horizontal y verticalmente: fillers en ambos ejes
    // absorben el espacio sobrante del panel.
    return ftxui::vbox({
        ftxui::filler(),
        ftxui::hbox({
            ftxui::filler(),
            std::move(board),
            ftxui::filler(),
        }),
        ftxui::filler(),
    });
}

// Ambos cuerpos reservan la misma cabecera y expanden el area del tablero.
Element didacticBoardBody(Element indicator, Element board) {
    return ftxui::vbox({std::move(indicator), ftxui::text(" "),
                        std::move(board) | ftxui::flex});
}

std::vector<Element> boardRows(
    const Game& game,
    std::size_t rows,
    std::size_t columns,
    bool colors, std::size_t rowOffset, std::size_t columnOffset, bool showViewport
) {
    std::vector<Element> output;
    rowOffset = std::min(rowOffset, game.height() - 1);
    columnOffset = std::min(columnOffset, game.width() - 1);
    const std::size_t visibleRows = std::min(rows, game.height() - rowOffset);
    const std::size_t visibleColumns = std::min(columns, game.width() - columnOffset);
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
            cells.push_back(cellElement(game.isAlive(row + rowOffset, column + columnOffset), colors));
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
    if (showViewport && (visibleRows < game.height() || visibleColumns < game.width())) {
        output.push_back(ftxui::text(
            "Viewport: " + std::to_string(visibleRows) + "/" +
            std::to_string(game.height()) + " filas, " +
            std::to_string(visibleColumns) + "/" + std::to_string(game.width()) +
            " columnas") | ftxui::dim);
    }
    if (showViewport && (rowOffset || columnOffset) && !output.empty()) {
        output.back() = ftxui::text("Viewport: filas " + std::to_string(rowOffset + 1) + "–" +
            std::to_string(rowOffset + visibleRows) + "/" + std::to_string(game.height()) +
            " · col. " + std::to_string(columnOffset + 1) + "–" +
            std::to_string(columnOffset + visibleColumns) + "/" + std::to_string(game.width())) | ftxui::dim;
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
    InteractiveApp(ftxui::ScreenInteractive& screen, DifferentialOutputBuffer& output,
                   ExperimentExecution::Backend backend = {}, std::unique_ptr<DidacticController> didactic = {})
        : screen_(screen),
          output_(output),
          mainMenu_(makeMenu(mainItems_, state_.mainSelection, [this] { selectMain(); })),
          benchmarkMenu_(makeMenu(benchmarkItems_, state_.benchmarkSelection,
                                  [this] { selectBenchmarkOption(); })),
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
          threadInput_(makeThreadInput()),
          mpiInput_(makeMPIInput()) {
        omp_set_num_threads(state_.requestedThreads);
        if(didactic) {
            state_.input=didactic->initialInput();state_.path="Captura didáctica";
            state_.didactic=std::move(didactic);
        }
        Add(mainMenu_);
        Add(benchmarkMenu_);
        Add(patternMenu_);
        Add(patternActionsMenu_);
        Add(engineMenu_);
        Add(threadMenu_);
        Add(pathInput_);
        Add(threadInput_);
        Add(mpiInput_);
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
        benchmarkItems_ = {
            "Comparación de motores", "Escalabilidad",
            "Resultados de la sesión",
        };
        engineItems_ = {"Secuencial", "OpenMP", "MPI", "Configurar procesos MPI", "Volver"};
        threadItems_ = {
            "1 hilo", "2 hilos", "4 hilos", "8 hilos",
            "Número personalizado", "Volver",
        };
        auto input = [this]() -> const InputData* {
            return state_.input ? &*state_.input : nullptr;
        };
        auto name = [this] { return std::filesystem::path(state_.path).filename().string(); };
        auto threads = [this] { return state_.requestedThreads; };
        auto style = [](Element row) {
            return colorsEnabled() ? row | ftxui::bgcolor(identityCyan()) |
                ftxui::color(ftxui::Color::RGB(25, 32, 38)) : row | ftxui::inverted;
        };
        experiment_ = std::make_unique<ExperimentExecution>([this] {
            screen_.PostEvent(Event::Custom);
        }, std::move(backend));
        auto submit = [this](bool scaling, ExperimentFormConfig config) {
            return launchExperiment(scaling, std::move(config));
        };
        comparisonForm_ = makeExperimentFormView(false, input, name, threads, style, submit);
        scalingForm_ = makeExperimentFormView(true, input, name, threads, style, submit);
        Add(comparisonForm_);
        Add(scalingForm_);
        refreshPatterns();
    }

    ~InteractiveApp() override {
        state_.simulation.reset();
        experiment_->shutdown();
        playbackTimer_.request_stop();
        // La salida normal ya confirmó el cierre asíncrono. La destrucción
        // une el worker ocioso y actúa como respaldo en una salida excepcional.
        state_.didactic.reset();
        if (benchmarkWorker_.joinable()) {
            benchmarkWorker_.join();
        }
    }

    Element Render() override {
        const auto size = controls::terminalSize();
        const bool compact = size.columns < 64 || size.rows < 18;
        Element top = ftxui::vbox({
            renderAppHeader(size.columns, size.rows) | ftxui::border,
            renderContextBar(state_, size.columns),
        });
        top->ComputeRequirement();
        // Los layouts existentes descuentan cuatro filas de cabecera total.
        // Compensar aqui su nueva altura mantiene intacto su presupuesto util.
        const auto extraHeaderRows = static_cast<std::size_t>(
            std::max(0, top->requirement().min_y - 4));
        const bool reduced = !ui::roomyTerminal(size.columns, size.rows);
        const auto extraFooterRows = reduced && !state_.message.empty()
            && state_.page != Page::ExperimentExecution && state_.page != Page::BenchmarkResults ? 1u : 0u;
        const auto helpWidth = static_cast<std::size_t>(ftxui::string_width(footerText()));
        const auto helpRows = reduced && size.columns > 2 ? (helpWidth + size.columns - 3) / (size.columns - 2) : 1;
        const auto overhead = extraHeaderRows + extraFooterRows + (helpRows > 1 ? helpRows - 1 : 0);
        const auto layoutHeight = size.rows > overhead ? size.rows - overhead : 0;
        Element content;
        switch (state_.page) {
            case Page::Main:
                content = renderMain();
                break;
            case Page::Patterns:
                content = renderPatterns(size.columns, layoutHeight, compact);
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
            case Page::MPIInput:
                content = renderInput("PROCESOS MPI", mpiInput_,
                    "P entre 1 y 1024 · Recomendados: " + std::to_string(recommendedMPIProcesses(state_.input ? state_.input->height : 1024)) +
                    " según afinidad/filas; slots del runtime pueden diferir. Sin sobresuscripción.");
                break;
            case Page::ThreadInput:
                content = renderInput("HILOS OPENMP", threadInput_,
                                      "Número entero entre 1 y " +
                                          std::to_string(kMaxOpenMPThreads));
                break;
            case Page::Simulation:
                content = renderSimulation(size.columns, layoutHeight, compact);
                break;
            case Page::BenchmarkMenu:
                content = renderBenchmarkMenu();
                break;
            case Page::ExperimentExecution:
                content = renderExperiment(false);
                break;
            case Page::ComparisonConfig:
                content = panel("COMPARACIÓN DE MOTORES", comparisonForm_->Render());
                break;
            case Page::BenchmarkScaling:
                content = panel("ESCALABILIDAD · OpenMP / MPI", scalingForm_->Render());
                break;
            case Page::BenchmarkResults:
                content = renderExperiment(true);
                break;
            case Page::Benchmark:
                content = renderBenchmark();
                break;
            case Page::Didactic:
                content = renderDidactic(size.columns, layoutHeight, compact);
                break;
        }

        const auto messageWidth = size.columns > helpWidth + 4 ? size.columns - helpWidth - 4 : 0;
        Element bottom = ftxui::hbox({
            ftxui::text(((state_.page == Page::ExperimentExecution || state_.page == Page::BenchmarkResults)
                && (showingComparison() || showingScaling())) ? "" : fitContextLabel(state_.message, messageWidth)) | ftxui::color(state_.message.starts_with("Error")
                ? ftxui::Color::Red : ftxui::Color::Green),
            ftxui::filler(),
            ftxui::text(footerText()) | ftxui::dim,
        });
        if (reduced) {
            ftxui::Elements lines;
            if (extraFooterRows) lines.push_back(ftxui::text(fitContextLabel(state_.message, size.columns - 2)) |
                ftxui::color(state_.message.starts_with("Error") ? ftxui::Color::Red : ftxui::Color::Green));
            lines.push_back(ftxui::paragraph(footerText()) | ftxui::dim);
            bottom = ftxui::vbox(std::move(lines));
        }
        if (!ui::usableTerminal(size.columns, size.rows)) {
            content = ftxui::vbox({ftxui::paragraph("Terminal insuficiente. Mínimo 40 × 18."),
                ftxui::paragraph("Amplía la ventana. El estado se conserva.")});
            bottom = ftxui::text("Esc volver · Q salir");
        }
        Element frame = ftxui::vbox({
            std::move(top),
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
            bool didacticBecameReady=false;
            if(state_.didactic) {
                const bool preparing=state_.didactic->isPreparing();state_.didactic->poll();
                didacticBecameReady=preparing && state_.didactic->isReady();
                if(!state_.didactic->errorMessage().empty()) state_.message=state_.didactic->errorMessage();
                if(didacticBecameReady && state_.page==Page::Didactic && state_.didactic->isPlaying()) armDidacticTimer();
            }
            if (state_.simulation) {
                state_.simulation->poll();
                if (!state_.simulation->error().empty()) state_.message = state_.simulation->error();
                else if (!state_.simulation->busy() &&
                    (state_.message.starts_with("Recuperando sesión") || state_.message.starts_with("Reiniciando sesión")))
                    state_.message = "Sesión lista · snapshot confirmado.";
                if (state_.simulation->closed() && state_.page == Page::Simulation) {
                    state_.page = Page::Main;
                    state_.message = "Sesión abandonada. La siguiente visualización comienza desde el patrón inicial.";
                }
            }
            if ((exitAfterSimulation_ || exitWhenExperimentFinishes_) && !experiment_->snapshot().active() &&
                (!state_.simulation || state_.simulation->closed()) && (!state_.didactic || state_.didactic->closed())) {
                screen_.Exit();
                return true;
            }
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
                           state_.didactic->isPlaying() && state_.didactic->isReady() && !didacticBecameReady) {
                    const auto now=std::chrono::steady_clock::now();
                    state_.didactic->tick(now-didacticTickAt_);didacticTickAt_=now;
                    if (state_.didactic->isPlaying() && state_.didactic->isReady()) schedulePlaybackTick();
                }
            }
            return true;
        }
        if ((state_.page == Page::ExperimentExecution || state_.page == Page::BenchmarkResults)
            && (showingComparison() || showingScaling()) && (event == Event::ArrowLeft || event == Event::ArrowRight)) {
            auto& tab = showingScaling() ? scalingTab_ : comparisonTab_;
            tab = (tab + (event == Event::ArrowRight ? 1 : 2)) % 3;
            resultRow_ = 0;
            return true;
        }
        if ((state_.page == Page::ExperimentExecution || state_.page == Page::BenchmarkResults)
            && (event == Event::ArrowDown || event == Event::ArrowUp || event == Event::Home || event == Event::End)) {
            if (event == Event::Home) resultRow_ = 0;
            else if (event == Event::End) resultRow_ = resultRows_ - 1;
            else resultRow_ += event == Event::ArrowDown ? 1 : -1;
            resultRow_ = std::clamp(resultRow_, 0, std::max(0, resultRows_ - 1));
            return true;
        }
        if (state_.page == Page::Didactic && state_.engine == Engine::MPI) {
            if(event.is_character()) {
                const auto k=event.character();
                if(k=="v"||k=="V"||k=="t"||k=="T") {mpiDidacticView_=(mpiDidacticView_+1)%3;mpiDidacticRow_=0;return true;}
                if(k=="["||k=="]") {
                    const auto capture=state_.didactic->capture();
                    const auto count=capture&&capture->mpiTrace?static_cast<int>(capture->mpiTrace->ranks.size()):state_.mpiProcesses;
                    mpiDidacticRank_=(mpiDidacticRank_+count+(k=="]"?1:-1))%count;mpiDidacticRow_=0;return true;
                }
            }
            if(event==Event::ArrowDown||event==Event::ArrowUp||event==Event::PageDown||event==Event::PageUp||event==Event::Home||event==Event::End) {
                if(event==Event::Home)mpiDidacticRow_=0;
                else if(event==Event::End)mpiDidacticRow_=100000;
                else mpiDidacticRow_=std::max(0,std::min(mpiDidacticRow_,mpiDidacticMaxOffset_)+(event==Event::ArrowDown?1:event==Event::ArrowUp?-1:event==Event::PageDown?5:-5));
                return true;
            }
        }
        const auto terminal = controls::terminalSize();
        const bool reduced = !ui::roomyTerminal(terminal.columns, terminal.rows);
        if (event.is_character() && (event.character() == "v" || event.character() == "V")) {
            if (state_.page == Page::Simulation) {
                if (simulationSingle_) simulationNext_ = !simulationNext_;
                return true;
            }
            if (state_.page == Page::Patterns && reduced) {
                if (catalogDetail_) catalogNext_ = !catalogNext_;
                return true;
            }
            if (state_.page == Page::Didactic && reduced) {
                if (!didacticWorkers_) didacticNext_ = !didacticNext_;
                return true;
            }
        }
        if (state_.page == Page::Patterns && event.is_character()
            && (event.character() == "d" || event.character() == "D")) {
            catalogDetail_ = !catalogDetail_; detailRow_ = 0; return true;
        }
        if (state_.page == Page::Didactic && event.is_character()
            && (event.character() == "t" || event.character() == "T")) {
            didacticWorkers_ = !didacticWorkers_; return true;
        }
        if (state_.page == Page::Patterns && catalogDetail_ && event == Event::Return) {
            selectPattern(); return true;
        }
        if (state_.page == Page::Simulation ||
            (state_.page == Page::Patterns && catalogDetail_) || state_.page == Page::Didactic) {
            if (event == Event::PageDown || event == Event::PageUp) {
                auto& scroll = state_.page == Page::Didactic ? workerRow_ : detailRow_;
                scroll = std::max(0, scroll + (event == Event::PageDown ? 1 : -1)); return true;
            }
            const int dy = event == Event::ArrowDown ? 1 : event == Event::ArrowUp ? -1 : 0;
            const int dx = event == Event::ArrowRight ? 1 : event == Event::ArrowLeft ? -1 : 0;
            if (dy || dx) {
                if (state_.page == Page::Didactic && didacticWorkers_) workerRow_ = std::max(0, workerRow_ + dy);
                else (state_.page == Page::Simulation ? simulationViewport_ : state_.page == Page::Patterns ? catalogViewport_ : didacticViewport_).move(dy,dx);
                return true;
            }
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
            if (key == "q" || key == "Q") {
                playbackTimer_.request_stop();
                state_.simulation->close();
                if (state_.simulation->closed()) state_.page = Page::Main;
                return true;
            }
            if (key == "u" || key == "U") {
                state_.message = "Recuperando sesión..."; state_.simulation->recover(); return true;
            }
            if (key == "x" || key == "X") { goBack(); return true; }
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
                state_.message = "Reiniciando sesión...";
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
                if (state_.didactic) state_.didactic->stepStage();
                return true;
            }
            if (key == " " || key == "p" || key == "P") {
                if (state_.didactic) {
                    state_.didactic->togglePlay();
                    if (state_.didactic->isPlaying() && state_.didactic->isReady()) {
                        armDidacticTimer();
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
        // Enter repetido permanece en la comparación, incluso al finalizar.
        // Esc vuelve al submenú sin iniciar otra medición accidentalmente.
        if (state_.page == Page::Benchmark && event == Event::Return) {
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
        if (event == Event::Return && state_.page == Page::MPIInput) {
            applyMPIDraft(); return true;
        }
        return activeComponent()->OnEvent(event);
    }

private:
    static constexpr unsigned kPlaybackEvent = 1U;
    static constexpr unsigned kBenchmarkEvent = 2U;

    void armDidacticTimer() {didacticTickAt_=std::chrono::steady_clock::now();schedulePlaybackTick();}

    void schedulePlaybackTick() {
        playbackTimer_.request_stop();
        playbackTimer_ = std::jthread([this](std::stop_token stopToken) {
            std::mutex mutex;std::condition_variable_any wake;std::unique_lock lock(mutex);
            wake.wait_for(lock,stopToken,150ms,[]{return false;});
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
        option.entries_option.transform = [](const ftxui::EntryState& entry) {
            Element row = ftxui::text((entry.active ? "> " : "  ") + entry.label);
            if (entry.active) row = row | ftxui::bold;
            if (entry.focused) {
                row = colorsEnabled()
                    ? row | ftxui::bgcolor(identityCyan()) |
                        ftxui::color(ftxui::Color::RGB(25, 32, 38))
                    : row | ftxui::inverted;
            }
            return row;
        };
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

    ftxui::Component makeMPIInput() {
        ftxui::InputOption option;
        option.multiline = false;
        option.placeholder = "1-1024";
        return ftxui::Input(&state_.mpiDraft, std::move(option));
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
            case Page::BenchmarkMenu: return benchmarkMenu_;
            case Page::ExperimentExecution: return benchmarkView_;
            case Page::ComparisonConfig: return comparisonForm_;
            case Page::BenchmarkScaling: return scalingForm_;
            case Page::BenchmarkResults: return benchmarkView_;
            case Page::Patterns:
                return state_.patternActionsFocused
                    ? patternActionsMenu_ : patternMenu_;
            case Page::Engines: return engineMenu_;
            case Page::Threads: return threadMenu_;
            case Page::MPIInput: return mpiInput_;
            case Page::PathInput: return pathInput_;
            case Page::ThreadInput: return threadInput_;
            case Page::Simulation: return simulationView_;
            case Page::Benchmark: return benchmarkView_;
            case Page::Didactic: return didacticView_;
        }
        return mainMenu_;
    }

    void refreshPatterns() {
        catalogDetail_ = false;
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
        catalogViewport_ = {};
        detailRow_ = 0;
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
            case 0: state_.page = Page::BenchmarkMenu; break;
            case 1: startSimulation(); break;
            case 2: state_.page = Page::Engines; break;
            case 3: state_.page = Page::Threads; break;
            case 4:
                if (experiment_->snapshot().active()) { state_.message = "Catálogo disponible al finalizar el experimento."; break; }
                state_.returnAfterCatalog = Page::Main;
                refreshPatterns();
                state_.page = Page::Patterns;
                break;
            case 5: startDidactic(); break;
            case 6: requestExit(); break;
            default: break;
        }
    }

    void selectBenchmarkOption() {
        switch (state_.benchmarkSelection) {
            case 0: state_.page = Page::ComparisonConfig; break;
            case 1: state_.page = Page::BenchmarkScaling; break;
            case 2: state_.page = Page::BenchmarkResults; break;
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
            } else if (state_.page == Page::BenchmarkMenu) {
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
        if (state_.engineSelection == 4) { state_.page = Page::Main; return; }
        if (state_.engineSelection == 3) {
            state_.mpiDraft = std::to_string(state_.mpiProcesses);
            state_.page = Page::MPIInput; return;
        }
        state_.engine = state_.engineSelection == 0 ? Engine::Sequential
            : state_.engineSelection == 1 ? Engine::OpenMP : Engine::MPI;
        if (state_.simulation && !state_.simulation->closed()) {
            state_.simulation->pause();
            // Selecting MPI does not launch it until Visualización is entered.
            if (state_.engine != Engine::MPI)
                state_.simulation->configure(state_.engine, state_.mpiProcesses);
        }
        state_.message = "Motor seleccionado: " + engineName(state_.engine);
        state_.page = Page::Main;
    }

    void applyMPIDraft() {
        const auto count = parseMPIProcesses(state_.mpiDraft);
        if (!count) { state_.message = "Error: P debe ser un entero entre 1 y 1024."; return; }
        state_.mpiProcesses = *count;
        state_.message = "Procesos MPI: " + std::to_string(*count) + " · se aplicarán al entrar en Visualización.";
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
            } else if (state_.page == Page::BenchmarkMenu) {
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
            if (state_.simulation && !state_.simulation->closed()) state_.simulation->close();
            if(state_.didactic && !state_.didactic->closed()) state_.didactic->close();
            replaceSimulation_ = true;
            state_.input = std::move(loaded);
            state_.path = path;
            state_.message = "Entrada cargada: " + path;
        } catch (const std::exception& error) {
            state_.message = "Error al cargar " + path + ": " + error.what();
        }
    }

    void startSimulation() {
        if(state_.didactic && state_.didactic->ownsMPISession()) {state_.message="Espera al cierre de la sesión didáctica MPI antes de visualizar.";return;}
        if (experiment_->snapshot().active()) { state_.message = "Simulación disponible al finalizar el experimento."; return; }
        if (!state_.input) {
            state_.returnAfterCatalog = Page::Simulation;
            refreshPatterns();
            state_.page = Page::Patterns;
            return;
        }
        createSimulation();
    }

    void createSimulation() {
        if(state_.didactic && state_.didactic->ownsMPISession()) {state_.message="Espera al cierre de la sesión didáctica MPI antes de visualizar.";state_.page=Page::Main;return;}
        simulationViewport_ = {};
        if (state_.simulation) state_.simulation->poll();
        if (replaceSimulation_ && state_.simulation && !state_.simulation->closed()) {
            state_.page = Page::Main;
            state_.message = "Cerrando la sesión anterior; vuelve a Visualización al terminar."; return;
        }
        if (!state_.simulation || state_.simulation->closed()) {
            state_.simulation = std::make_unique<SimulationSession>(*state_.input, state_.engine,
                defaultMPIOptions(state_.mpiProcesses), [this] { screen_.PostEvent(Event::Custom); });
            replaceSimulation_ = false;
        } else if (state_.simulation->engine() != state_.engine || state_.simulation->processes() != state_.mpiProcesses) {
            state_.simulation->configure(state_.engine, state_.mpiProcesses);
        }
        state_.effectiveThreads = 0;
        state_.page = Page::Simulation;
    }

    void startDidactic() {
        if(state_.simulation && !state_.simulation->closed() && (state_.simulation->engine()==Engine::MPI || state_.simulation->busy())) {state_.message="Abandona la sesión MPI con Q antes de entrar en didáctica.";return;}
        if (experiment_->snapshot().active()) { state_.message = "Modo didáctico disponible al finalizar el experimento."; return; }
        if (!state_.input) {
            state_.returnAfterCatalog = Page::Didactic;
            refreshPatterns();
            state_.page = Page::Patterns;
            return;
        }
        createDidactic();
    }

    void createDidactic() {
        didacticViewport_ = {};
        mpiDidacticView_=0; mpiDidacticRank_=0; mpiDidacticRow_=0;
        state_.message.clear();
        if (!state_.input) return;
        if(state_.simulation && !state_.simulation->closed() && (state_.simulation->engine()==Engine::MPI || state_.simulation->busy())) {state_.page=Page::Main;state_.message="Abandona la sesión MPI con Q antes de entrar en didáctica.";return;}
        if(!state_.didactic) state_.didactic=std::make_unique<DidacticController>([this]{screen_.PostEvent(Event::Custom);});
        state_.didactic->startPreparation(*state_.input,state_.engine,state_.requestedThreads,defaultMPIOptions(state_.mpiProcesses),
            state_.engine==Engine::MPI ? (state_.input->height+state_.mpiProcesses-1)/state_.mpiProcesses-1 : 0,
            state_.input->width/2);
        state_.page = Page::Didactic;
    }

    void startBenchmark() {
        if(state_.didactic && state_.didactic->ownsMPISession()) {state_.message="Espera al cierre de la sesión didáctica MPI antes de medir.";return;}
        if(state_.didactic && state_.didactic->busy()) {state_.message="Espera al cierre o captura didáctica antes de medir.";return;}
        if (state_.simulation && !state_.simulation->closed() && (state_.simulation->engine() == Engine::MPI || state_.simulation->busy())) { state_.message = "Abandona la sesión MPI con Q antes de medir."; return; }
        if (experiment_->snapshot().active()) return;
        if (state_.benchmarkRunning) {
            state_.page = Page::Benchmark;
            return;
        }
        if (!state_.input) {
            state_.returnAfterCatalog = Page::BenchmarkMenu;
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
                requestExit();
                return true;
            }
            if (key == "c" || key == "C") {
                if (experiment_->snapshot().active()) { state_.message = "Catálogo disponible al finalizar el experimento."; return true; }
                state_.returnAfterCatalog = Page::Main;
                refreshPatterns();
                state_.page = Page::Patterns;
                return true;
            }
            if (key == "1") { state_.page = Page::BenchmarkMenu; return true; }
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
            if (key == "m" || key == "M") { state_.engineSelection = 2; selectEngine(); return true; }
            if (key == "c" || key == "C") { state_.engineSelection = 3; selectEngine(); return true; }
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
                requestExit();
                break;
            case Page::Patterns:
                state_.page = state_.returnAfterCatalog;
                break;
            case Page::Engines:
            case Page::Threads:
            case Page::BenchmarkMenu:
                state_.page = Page::Main;
                break;
            case Page::ExperimentExecution:
                state_.page = experiment_->snapshot().description.scaling
                    ? Page::BenchmarkScaling : Page::ComparisonConfig;
                break;
            case Page::Benchmark:
            case Page::ComparisonConfig:
            case Page::BenchmarkScaling:
            case Page::BenchmarkResults:
                state_.page = Page::BenchmarkMenu;
                break;
            case Page::PathInput:
                state_.page = pathReturnPage_;
                break;
            case Page::MPIInput:
                state_.page = Page::Engines; break;
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
                    state_.didactic->close();
                }
                state_.page = Page::Main;
                break;
        }
    }

    Element renderMain() {
        return panel("MENÚ PRINCIPAL", mainMenu_->Render() | ftxui::flex);
    }

    Element scrollDetail(std::vector<Element> rows) {
        detailRow_ = std::clamp(detailRow_, 0, std::max(0, static_cast<int>(rows.size()) - 1));
        if (!rows.empty()) rows[static_cast<std::size_t>(detailRow_)] = rows[static_cast<std::size_t>(detailRow_)] | ftxui::focus;
        return ftxui::vbox(std::move(rows)) | ftxui::yframe | ftxui::xflex | ftxui::yflex_shrink;
    }

    Element viewportLabel(const ui::BoardViewport& view, std::size_t height, std::size_t width,
                          std::size_t rows, std::size_t columns) {
        return ftxui::text("Viewport: filas " + std::to_string(view.row + 1) + "–" +
            std::to_string(std::min(height, view.row + rows)) + "/" + std::to_string(height) +
            " · col. " + std::to_string(view.column + 1) + "–" +
            std::to_string(std::min(width, view.column + columns)) + "/" + std::to_string(width)) | ftxui::dim;
    }

    Element catalogBoards(bool paired) {
        if (!catalogPreview_.current || !catalogPreview_.next)
            return ftxui::text("Sin vista previa.");
        return ui::allocatedElement([this, paired](int width, int height) {
            const auto& current = *catalogPreview_.current;
            const int panelWidth = paired ? (width - 1) / 2 : width;
            // Panel: two borders, title and separator. Grid: 2*r+1,
            // plus one viewport line even when the origin is zero.
            if (height < (paired ? 8 : 5) || panelWidth < 4)
                return ftxui::text("Tablero sin espacio. Amplía la terminal.");
            const auto rows = static_cast<std::size_t>((height - (paired ? 6 : 3)) / 2);
            const auto columns = static_cast<std::size_t>((panelWidth - (paired ? 3 : 1)) / 3);
            catalogViewport_.clamp(current.height(), current.width(), rows, columns);
            auto board = [&](bool next) {
                auto body = ftxui::vbox(boardRows(next ? *catalogPreview_.next : current,
                    rows, columns, colorsEnabled(), catalogViewport_.row, catalogViewport_.column, false));
                const auto title = next ? "SIGUIENTE · generación 1" : "ACTUAL · generación 0";
                return paired ? panel(title, std::move(body))
                    : ftxui::vbox({heading(title), std::move(body) | ftxui::flex});
            };
            Element boards = paired ? ftxui::hbox({board(false) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, panelWidth) | ftxui::flex,
                ftxui::text(" "), board(true) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, panelWidth) | ftxui::flex}) : board(catalogNext_);
            return ftxui::vbox({std::move(boards) | ftxui::flex,
                viewportLabel(catalogViewport_, current.height(), current.width(), rows, columns)});
        });
    }

    Element renderCompactPatterns(Element list, std::size_t width, std::size_t height) {
        if (!catalogDetail_) return ftxui::vbox({heading("CATÁLOGO DE PATRONES"), std::move(list) | ftxui::flex});
        std::vector<Element> rows;
        if (catalog_.entries.empty()) rows.push_back(ftxui::paragraph("No hay patrones disponibles."));
        else {
            const auto& entry = catalog_.entries[static_cast<std::size_t>(state_.patternSelection)];
            rows.push_back(ftxui::paragraph("Patrón: " + entry.name));
            if (!entry.valid()) rows.push_back(ftxui::paragraph(entry.error) | ftxui::color(ftxui::Color::Red));
            else {
                rows.push_back(ftxui::paragraph("Dimensiones: " + std::to_string(entry.height) + " × " +
                    std::to_string(entry.width) + " · " + std::to_string(entry.aliveCells) + " vivas"));
                if (!entry.type.empty()) rows.push_back(ftxui::paragraph("Tipo: " + entry.type));
                if (entry.period) rows.push_back(ftxui::text("Período: " + std::to_string(*entry.period)));
                if (!entry.description.empty()) rows.push_back(ftxui::paragraph(entry.description) | ftxui::dim);
                if (!catalogPreview_.error.empty()) rows.push_back(ftxui::paragraph(catalogPreview_.error));
            }
        }
        auto metadata = scrollDetail(std::move(rows)) | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 5);
        return panel("DETALLE ACTIVO · D lista · V vista", ftxui::vbox({
            std::move(metadata), catalogBoards(false) | ftxui::flex}));
    }

    Element renderPatterns(std::size_t width, std::size_t height, bool compact) {
        Element patternEntries = patternMenu_->Render();
        if (!ui::roomyTerminal(controls::terminalSize().columns, controls::terminalSize().rows))
            patternEntries = std::move(patternEntries) | ftxui::yframe;
        Element listBody = ftxui::vbox({
            ftxui::text("Archivos .txt disponibles") | ftxui::dim,
            catalog_.error.empty()
                ? std::move(patternEntries) | ftxui::flex
                : ftxui::text("Error: " + catalog_.error) | ftxui::color(ftxui::Color::Red),
            ftxui::separator(),
            ftxui::text("ACCIONES") | ftxui::bold,
            patternActionsMenu_->Render(),
        });
        Element patternList = panel(catalogDetail_ ? "PATRONES" : "PATRONES ACTIVOS · D detalle", std::move(listBody));
        if (!ui::roomyTerminal(controls::terminalSize().columns, controls::terminalSize().rows))
            return renderCompactPatterns(std::move(patternList), width, height);

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
                    details.push_back(ftxui::paragraph(selected.description) | ftxui::dim);
                }
            }
        } else {
            details.push_back(ftxui::text("Selecciona un archivo .txt para ver sus datos."));
        }

        Element detailContent = scrollDetail(std::move(details)) |
            ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 5);
        if (!catalogPreview_.error.empty()) {
            detailContent = ftxui::vbox({
                std::move(detailContent),
                ftxui::text("Vista previa no disponible: " + catalogPreview_.error) |
                    ftxui::color(ftxui::Color::Red),
            });
        } else if (catalogPreview_.current && catalogPreview_.next) {
            detailContent = ftxui::vbox({std::move(detailContent), ftxui::separator(),
                catalogBoards(true) | ftxui::flex});
        }
        Element detailPanel = panel(catalogDetail_ ? "DETALLE ACTIVO · D lista" : "DETALLE · D activar", std::move(detailContent));

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
        return panel("MOTOR DE EJECUCIÓN", engineMenu_->Render() | ftxui::flex);
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
        if (state_.simulation->engine() == Engine::MPI || !state_.simulation->preview()) {
            simulationSingle_ = false;
            const auto rows = std::max<std::size_t>(1, std::min(game.height(),height>12?(height-12)/2:1));
            const auto columns = std::max<std::size_t>(1, (width > 6 ? width - 6 : 1) / 3);
            simulationViewport_.clamp(game.height(), game.width(), rows, columns);
            const auto status = state_.simulation->busy() ? (state_.simulation->isRunning() ? "Calculando" : "Operación pendiente")
                : state_.simulation->isRunning() ? "En ejecución" : "Pausado";
            return panel("SIMULACIÓN · " + engineName(state_.simulation->engine()) +
                (state_.simulation->engine() == Engine::MPI ? " · P=" + std::to_string(state_.mpiProcesses) : " · cambiando motor"), ftxui::vbox({
                ftxui::text("Generación " + std::to_string(game.generation()) + " · " + std::to_string(game.aliveCount()) + " vivas · " + status),
                panel("CURRENT · snapshot confirmado", ftxui::vbox(boardRows(game, rows, columns, colorsEnabled(), simulationViewport_.row, simulationViewport_.column))) | ftxui::flex,
                ftxui::paragraph(state_.simulation->error()) | ftxui::color(ftxui::Color::Red),
                ftxui::text("N paso · Esp pausa/reanuda · R reiniciar · U recuperar · Esc menú · Q abandonar"),
            }));
        }
        const bool single = !ui::roomyTerminal(controls::terminalSize().columns, controls::terminalSize().rows)
            && !ui::pairedSimulation(width, height, game.width(), game.height());
        simulationSingle_ = single;
        const std::size_t rows = visibleBoardRows(single && height > 2 ? height - 2 : height, game.height(), single ? false : compact);
        const std::size_t panelWidth = single ? (width > 6 ? width - 6 : 1) : compact
            ? std::max<std::size_t>(1, width > 8 ? width - 8 : 1)
            : std::max<std::size_t>(
                  1, (width - std::min<std::size_t>(width, 10)) / 2);
        const std::size_t columns = std::max<std::size_t>(
            1, std::min(game.width(), single ? (panelWidth - 1) / 3 : panelWidth / 4));
        simulationViewport_.clamp(game.height(), game.width(), rows, columns);
        const bool colors = colorsEnabled();
        Element current = panel("CURRENT · generación " +
            std::to_string(game.generation()),
            ftxui::vbox(boardRows(game, rows, columns, colors, simulationViewport_.row, simulationViewport_.column)));
        Element next = panel("NEXT · generación " +
            std::to_string(game.generation() + 1),
            ftxui::vbox(boardRows(*state_.simulation->preview(), rows, columns, colors, simulationViewport_.row, simulationViewport_.column)));
        Element boards = single ? (simulationNext_ ? std::move(next) : std::move(current)) : compact
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

    void requestExit() {
        exitAfterSimulation_ = true;
        if (state_.simulation && !state_.simulation->closed()) state_.simulation->close();
        if(state_.didactic && !state_.didactic->closed()) state_.didactic->close();
        if (experiment_->snapshot().active()) {
            exitWhenExperimentFinishes_ = true;
            state_.message = "Salida pendiente: se cerrará al finalizar el experimento.";
        } else if ((!state_.simulation || state_.simulation->closed()) && (!state_.didactic || state_.didactic->closed())) screen_.Exit();
    }

    std::size_t retainedUiPayload() const {
        using detail::checkedAdd;
        using detail::checkedMultiply;
        auto bytes = state_.input ? state_.input->cells.capacity() : 0;
        auto gameBytes = [&](const Game& game) {
            return checkedMultiply(2, checkedMultiply(checkedAdd(game.width(), 2), checkedAdd(game.height(), 2)));
        };
        for (const auto* game : {&catalogPreview_.current, &catalogPreview_.next})
            if (*game) bytes = checkedAdd(bytes, gameBytes(**game));
        if (state_.simulation) {
            const auto& game = state_.simulation->game();
            bytes = checkedAdd(bytes, checkedAdd(gameBytes(game), checkedMultiply(game.width(), game.height())));
            if (state_.simulation->preview()) bytes = checkedAdd(bytes,gameBytes(*state_.simulation->preview()));
        }
        if (state_.didactic) {
            bytes = checkedAdd(bytes, state_.didactic->initialInput().cells.capacity());
            for (const auto* game : {state_.didactic->preStep(), state_.didactic->postStep()})
                if (game) bytes = checkedAdd(bytes, gameBytes(*game));
            if (const auto* trace = state_.didactic->stepTrace())
                bytes = checkedAdd(bytes, checkedMultiply(trace->workers.capacity(), sizeof(core::WorkerRecord)));
        }
        if (state_.benchmark)
            bytes = checkedAdd(bytes, checkedMultiply(state_.benchmark->samples.capacity(), sizeof(BenchmarkSample)));
        return bytes;
    }

    std::string launchExperiment(bool scaling, ExperimentFormConfig config) {
        if(state_.didactic && state_.didactic->ownsMPISession()) return "Espera al cierre de la sesión didáctica MPI antes de medir.";
        if (state_.simulation && !state_.simulation->closed() && (state_.simulation->engine() == Engine::MPI || state_.simulation->busy())) return "Abandona la sesión MPI con Q antes de medir.";
        if (experiment_->snapshot().active()) return "Ya hay un experimento activo. Consulta Resultados de la sesión.";
        if (exitWhenExperimentFinishes_) return "La aplicación está pendiente de cierre.";
        if (state_.benchmarkRunning || (state_.didactic && state_.didactic->busy()))
            return "Espera a que termine la preparación o medición activa.";
        try {
            const auto bytes = retainedUiPayload();
            if (!experiment_->start(scaling, std::move(config), state_.input ? &*state_.input : nullptr,
                                    std::filesystem::path(state_.path).filename().string(), bytes))
                return "Ya hay un experimento activo.";
            resultRow_ = 0; comparisonTab_=0; scalingTab_=0;
            state_.page = Page::ExperimentExecution;
            return {};
        } catch (const std::exception& error) { return error.what(); }
    }

    bool showingComparison() const {
        const auto snapshot = experiment_->snapshot();
        return snapshot.latest && std::holds_alternative<ExperimentResult>(snapshot.latest->result)
            && (state_.page == Page::BenchmarkResults || snapshot.phase == ExecutionPhase::Finished ||
                (snapshot.phase == ExecutionPhase::Error && snapshot.latest->description.id == snapshot.description.id));
    }

    bool showingScaling() const {
        const auto snapshot = experiment_->snapshot();
        return snapshot.latest && std::holds_alternative<ScalingResult>(snapshot.latest->result)
            && (state_.page == Page::BenchmarkResults || snapshot.phase == ExecutionPhase::Finished ||
                (snapshot.phase == ExecutionPhase::Error && snapshot.latest->description.id == snapshot.description.id));
    }

    // Resolve paragraph heights at the viewport width before yframe computes
    // the selected row's position. Otherwise wrapped paragraphs report stale
    // heights and the frame can keep showing the first screen.
    Element resultViewport(std::vector<Element> rows, Element tabs) {
        const int width = std::max(1, static_cast<int>(controls::terminalSize().columns) - 3);
        for (auto& row : rows) {
            auto probe = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fit(row));
            ftxui::Render(probe, row);
            row = row | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, std::max(1, probe.dimy()));
        }
        resultRows_ = static_cast<int>(rows.size());
        resultRow_ = std::clamp(resultRow_, 0, std::max(0, resultRows_ - 1));
        if (!rows.empty()) rows[resultRow_] = rows[resultRow_] | ftxui::focus;
        return ftxui::vbox({std::move(tabs), ftxui::separator(),
            ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator | ftxui::yframe | ftxui::flex,
            ftxui::text("Bloque " + std::to_string(resultRow_ + 1) + "/" + std::to_string(resultRows_) +
                " · ↑↓ desplazar · Inicio/Fin") | ftxui::dim});
    }

    Element renderScaling(const ExecutionSnapshot& snapshot, bool session) {
        const auto& stored = *snapshot.latest;
        std::vector<Element> rows;
        if (snapshot.phase != ExecutionPhase::Finished) {
            const char* status = snapshot.phase == ExecutionPhase::Error ? "Error" :
                snapshot.phase == ExecutionPhase::Preparing ? "Preparando entrada" : "Ejecutando mediciones";
            rows.push_back(ftxui::paragraph(std::string("Estado: ") + status +
                " · ejecución actual #" + std::to_string(snapshot.description.id)));
            if (!snapshot.error.empty()) rows.push_back(ftxui::paragraph(snapshot.error) | ftxui::color(ftxui::Color::Red));
            rows.push_back(ftxui::text("Resultado conservado · ejecución #" + std::to_string(stored.description.id)));
        } else rows.push_back(ftxui::text("Estado: Finalizado · Ejecución #" + std::to_string(stored.description.id)) |
            ftxui::color(ftxui::Color::RGB(98,232,137)));
        auto presentation = scalingRows(std::get<ScalingResult>(stored.result), stored.description.patternName,
            static_cast<ScalingTab>(scalingTab_), static_cast<int>(controls::terminalSize().columns) - 2,
            identityCyan(), ftxui::Color::RGB(98,232,137));
        auto tabs = presentation.front();
        rows.insert(rows.end(), std::make_move_iterator(presentation.begin() + 1), std::make_move_iterator(presentation.end()));
        rows.push_back(ftxui::paragraph("Contexto conservado: " + stored.context.studyId) | ftxui::dim);
        return panel(session ? "RESULTADOS DE LA SESIÓN" :
            std::get<ScalingResult>(stored.result).config.parallelEngine==Engine::MPI?"ESCALABILIDAD MPI":"ESCALABILIDAD OPENMP",
            resultViewport(std::move(rows), std::move(tabs)));
    }

    Element renderComparison(const ExecutionSnapshot& snapshot, bool session) {
        const auto& stored = *snapshot.latest;
        std::vector<Element> rows;
        if (snapshot.phase != ExecutionPhase::Finished) {
            const char* status = snapshot.phase == ExecutionPhase::Error ? "Error" :
                snapshot.phase == ExecutionPhase::Preparing ? "Preparando entrada" : "Ejecutando mediciones";
            rows.push_back(ftxui::paragraph(std::string("Estado: ") + status +
                " · ejecución actual #" + std::to_string(snapshot.description.id)));
            if (!snapshot.error.empty()) rows.push_back(ftxui::paragraph(snapshot.error) | ftxui::color(ftxui::Color::Red));
        } else rows.push_back(ftxui::text("Estado: Finalizado · Ejecución #" + std::to_string(stored.description.id)) |
            ftxui::color(ftxui::Color::RGB(98,232,137)));
        if (snapshot.phase != ExecutionPhase::Finished)
            rows.push_back(ftxui::text("Resultado conservado · ejecución #" + std::to_string(stored.description.id)));
        auto presentation = comparisonRows(std::get<ExperimentResult>(stored.result), stored.description.patternName,
            static_cast<ComparisonTab>(comparisonTab_), static_cast<int>(controls::terminalSize().columns) - 2,
            identityCyan(), ftxui::Color::RGB(98,232,137));
        auto tabs = presentation.front();
        rows.insert(rows.end(), std::make_move_iterator(presentation.begin() + 1), std::make_move_iterator(presentation.end()));
        return panel(session ? "RESULTADOS DE LA SESIÓN" : "COMPARACIÓN DE MOTORES",
            resultViewport(std::move(rows), std::move(tabs)));
    }

    Element renderExperiment(bool session) {
        const auto snapshot = experiment_->snapshot();
        if (snapshot.latest && std::holds_alternative<ExperimentResult>(snapshot.latest->result)
            && (session || snapshot.phase == ExecutionPhase::Finished ||
                (snapshot.phase == ExecutionPhase::Error && snapshot.latest->description.id == snapshot.description.id))) return renderComparison(snapshot, session);
        if (snapshot.latest && std::holds_alternative<ScalingResult>(snapshot.latest->result)
            && (session || snapshot.phase == ExecutionPhase::Finished ||
                (snapshot.phase == ExecutionPhase::Error && snapshot.latest->description.id == snapshot.description.id))) return renderScaling(snapshot, session);
        const auto& d = snapshot.description;
        std::vector<Element> rows;
        const char* status = "Listo";
        switch (snapshot.phase) {
            case ExecutionPhase::Ready: break;
            case ExecutionPhase::Preparing: status = "Preparando entrada"; break;
            case ExecutionPhase::Running: status = "Ejecutando mediciones"; break;
            case ExecutionPhase::Finished: status = "Finalizado"; break;
            case ExecutionPhase::Error: status = "Error"; break;
        }
        rows.push_back(ftxui::text(std::string("Estado: ") + status) |
            ftxui::color(snapshot.phase == ExecutionPhase::Finished
                ? ftxui::Color::RGB(98, 232, 137) : identityCyan()));
        if (d.id) {
            rows.push_back(ftxui::text("Ejecución #" + std::to_string(d.id) + " · " +
                (d.scaling ? (d.config.scaling.parallelEngine==Engine::MPI?"Escalabilidad MPI":"Escalabilidad OpenMP") : "Comparación de motores")));
            rows.push_back(ftxui::paragraph("Entrada: " + (d.config.generated ? std::string("Tablero generado") : d.patternName) +
                " · " + std::to_string(d.width) + " × " + std::to_string(d.height) + " (ancho × alto)"));
            const auto& c = d.config.comparison;
            const auto& sc = d.config.scaling;
            rows.push_back(ftxui::paragraph("Generaciones: " + std::to_string(d.scaling ? sc.generations : c.generations) +
                " · Calentamientos: " + std::to_string(d.scaling ? sc.warmups : c.warmups) +
                " · Repeticiones: " + std::to_string(d.scaling ? sc.repetitions : c.repetitions)));
            std::string resources;
            if(d.scaling) {
                resources=sc.parallelEngine==Engine::MPI?"Procesos solicitados: ":"Hilos solicitados: ";
                for(auto p:sc.parallelEngine==Engine::MPI?sc.requestedProcesses:sc.requestedThreads) resources+=std::to_string(p)+" ";
            } else {
                resources="Participantes: ";
                for(const auto& participant:c.participants) resources+=configurationName(participant)+" ";
            }
            rows.push_back(ftxui::paragraph(resources));
        }
        if (!snapshot.error.empty()) rows.push_back(ftxui::paragraph(snapshot.error) | ftxui::color(ftxui::Color::Red));
        if (snapshot.latest && (session || snapshot.phase == ExecutionPhase::Finished)) {
            const auto& stored = *snapshot.latest;
            rows.push_back(ftxui::separator());
            rows.push_back(ftxui::text("Resultado conservado · ejecución #" + std::to_string(stored.description.id)));
            auto number = [](double value) { std::ostringstream out; out << std::setprecision(8) << value; return out.str(); };
            auto summary = [&](std::string label, const ExperimentSummary& data) {
                rows.push_back(ftxui::text(std::move(label)));
                rows.push_back(ftxui::text(std::string("Equivalencia: ") + (data.equivalent ? "SI" : "NO") +
                    " · Validez: " + (data.validForComparison ? "SI" : "NO")));
                if (data.sequentialStatistics) rows.push_back(ftxui::text("Mediana secuencial: " + number(data.sequentialStatistics->medianMs) + " ms"));
                if (data.openMPStatistics) rows.push_back(ftxui::text("Mediana OpenMP: " + number(data.openMPStatistics->medianMs) + " ms"));
                rows.push_back(ftxui::text("Speedup: " + (data.speedup ? number(*data.speedup) : "N/D") +
                    " · Eficiencia: " + (data.efficiency ? number(*data.efficiency) : "N/D")));
            };
            std::visit([&](const auto& result) {
                if constexpr (std::is_same_v<std::decay_t<decltype(result)>, ExperimentResult>) summary("Comparación de motores", result.summary);
                else for (const auto& point : result.summary.points)
                    summary("OpenMP · " + std::to_string(point.requestedThreads) + " hilos solicitados", point.summary);
            }, stored.result);
            rows.push_back(ftxui::text("Contexto conservado: " + stored.context.studyId) | ftxui::dim);
        }
        resultRows_ = static_cast<int>(rows.size());
        resultRow_ = std::clamp(resultRow_, 0, std::max(0, resultRows_ - 1));
        if (!rows.empty()) rows[static_cast<std::size_t>(resultRow_)] =
            rows[static_cast<std::size_t>(resultRow_)] | ftxui::focus;
        return panel(session ? "RESULTADOS DE LA SESIÓN · ÚLTIMO EXPERIMENTO" : "EXPERIMENTO",
                     ftxui::vbox(std::move(rows)) | ftxui::yframe | ftxui::flex);
    }

    Element renderBenchmarkMenu() {
        static const std::array<const char*, 3> descriptions{
            "Selecciona participantes entre Secuencial, OpenMP y MPI.",
            "Configura escalabilidad OpenMP (hilos) o MPI (procesos).",
            "Consulta la ejecución activa y el último resultado conservado.",
        };
        return panel("BENCHMARK", ftxui::vbox({
            benchmarkMenu_->Render(),
            ftxui::separator(),
            ftxui::paragraph(descriptions.at(
                static_cast<std::size_t>(state_.benchmarkSelection))) | ftxui::dim,
        }));
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
            ftxui::text("Pulsa Esc para volver.") | ftxui::dim);
        return panel("BENCHMARK · " + activePattern(state_),
            ftxui::vbox(std::move(reportRows)));
    }

    // Render del modo didactico. Esta vista NO recalcula reglas de
    // Conway; solo muestra la StepTrace capturada por el worker y
    // la revelacion progresiva basada en el reloj de reproduccion.
    Element renderMPIDidactic(std::shared_ptr<const DidacticFrame> snapshot) {
        const auto& f=*snapshot;
        const bool tiny=controls::terminalSize().columns<=44 || controls::terminalSize().rows<=20;
        const auto stage=f.stages.empty()?DidacticStage::MPIState:f.stages.at(f.stage_index);
        std::vector<Element> status{
            ftxui::paragraph("Captura MPI real; orden de presentación didáctico"),
            ftxui::paragraph("Visible G"+std::to_string(f.viewed_generation)+(tiny?std::string{}:" · capturada "+(f.captured_generation?std::to_string(*f.captured_generation):"pendiente"))+
                " · "+(f.playing?"Reproduciendo":"En pausa")+" · "+speedLabel(f.speed)),
            ftxui::paragraph(f.stages.empty()?"Etapa pendiente":("Etapa "+std::to_string(f.stage_index+1)+"/"+std::to_string(f.stages.size())+" · "+didactic_view::stage(stage)))};
        if(!f.error_message.empty())status.push_back(ftxui::paragraph("ERROR: captura detenida. Diagnóstico abajo.")|ftxui::color(ftxui::Color::Red));
        if(f.is_preparing)status.push_back(ftxui::text("Capturando; UI y pausa disponibles."));
        Element content=ftxui::text("Sin captura disponible. R reinicia; Esc cierra.");
        {
            const auto trace=f.mpi_trace;
            if(trace)mpiDidacticRank_=std::clamp(mpiDidacticRank_,0,static_cast<int>(trace->ranks.size())-1);
            const bool result=stage==DidacticStage::GenerationResult;
            const bool swapped=result||stage==DidacticStage::MPIBuffers;
            content=ui::allocatedElement([this,trace,result,swapped,stage,error=f.error_message](int w,int h) {
                const bool multiple=trace && error.empty() && w>=110 && h>=18 && mpiDidacticView_!=2;
                std::vector<didactic_view::Rows> columns;
                if(!error.empty())columns.push_back({"Diagnóstico de captura MPI",error,"No se reintenta el paso automáticamente. R reinicia; Esc cierra."});
                else if(!trace)columns.push_back({"Sin captura disponible; esperando al trabajador.","R reinicia; Esc cierra."});
                else if(multiple) {
                    columns.push_back(didactic_view::global(*trace,result));
                    const auto capacity=std::max(1,std::min(static_cast<int>(trace->ranks.size()),w/38-1));
                    const int first=(mpiDidacticRank_/capacity)*capacity;
                    for(int r=first;r<std::min(first+capacity,static_cast<int>(trace->ranks.size()));++r)
                        columns.push_back(didactic_view::local(*trace,r,swapped));
                } else if(mpiDidacticView_==0)columns.push_back(didactic_view::global(*trace,result));
                else if(mpiDidacticView_==1)columns.push_back(didactic_view::local(*trace,mpiDidacticRank_,swapped));
                else columns.push_back(didactic_view::explanation(*trace,mpiDidacticRank_,stage));
                // Wrap into physical rows before scrolling. This makes every line,
                // including long buffer contents, reachable after a resize.
                const int cw=std::max(8,w/static_cast<int>(columns.size())-3);
                std::vector<std::vector<std::string>> wrapped;
                int maxRows=0;
                for(const auto& col:columns) {
                    std::vector<std::string> lines;
                    for(const auto& line:col) {
                        std::string row;
                        int cells=0;
                        for(std::size_t offset=0;offset<line.size();) {
                            std::size_t end=offset+1;
                            while(end<line.size() && (static_cast<unsigned char>(line[end])&0xc0)==0x80)++end;
                            const auto glyph=line.substr(offset,end-offset);
                            if(glyph=="\n") {lines.push_back(std::move(row));row.clear();cells=0;offset=end;continue;}
                            const int width=ftxui::string_width(glyph);
                            if(cells+width>cw && !row.empty()) {
                                const auto space=row.rfind(' ');
                                if(space!=std::string::npos && space>row.size()/2) {
                                    lines.push_back(row.substr(0,space));row=row.substr(space+1);cells=ftxui::string_width(row);
                                } else {lines.push_back(std::move(row));row.clear();cells=0;}
                            }
                            row+=glyph;cells+=width;offset=end;
                        }
                        lines.push_back(std::move(row));
                    }
                    maxRows=std::max(maxRows,static_cast<int>(lines.size()));wrapped.push_back(std::move(lines));
                }
                const int visible=std::max(1,h-1);
                mpiDidacticMaxOffset_=std::max(0,maxRows-visible);
                const int start=std::clamp(mpiDidacticRow_,0,mpiDidacticMaxOffset_);
                std::vector<Element> panels;
                for(const auto& lines:wrapped) {
                    std::vector<Element> rows;
                    for(int y=start;y<std::min(start+visible,static_cast<int>(lines.size()));++y)rows.push_back(ftxui::text(lines[y]));
                    panels.push_back(ftxui::vbox(std::move(rows))|ftxui::size(ftxui::WIDTH,ftxui::EQUAL,cw)|ftxui::flex);
                    if(panels.size()<wrapped.size()*2-1)panels.push_back(ftxui::separator());
                }
                return ftxui::vbox({ftxui::hbox(std::move(panels))|ftxui::flex,
                    ftxui::text("Líneas "+std::to_string(start+1)+"-"+std::to_string(std::min(maxRows,start+visible))+"/"+std::to_string(maxRows)+" · ↑↓ Home/End")});
            });
        }
        static constexpr const char* views[]={"Global","Local","Explicación"};
        status.push_back(ftxui::paragraph(std::string("Vista ")+views[mpiDidacticView_]+(tiny?" · R":" · rank R")+std::to_string(mpiDidacticRank_)+
            (tiny?" · P":" · P sol/obs ")+std::to_string(f.requested_processes)+"/"+std::to_string(f.observed_processes)));
        if(tiny)return ftxui::vbox({ftxui::vbox(std::move(status)),std::move(content)|ftxui::flex})|ftxui::border;
        return panel("DIDÁCTICA MPI",ftxui::vbox({ftxui::vbox(std::move(status)),std::move(content)|ftxui::flex}));
    }

    Element renderCompactDidactic(std::shared_ptr<const DidacticFrame> snapshot) {
        const auto& frame = *snapshot;
        std::vector<Element> rows;
        rows.push_back(ftxui::paragraph("REPRODUCCIÓN DIDÁCTICA · No es ejecución en vivo") | ftxui::color(ftxui::Color::Cyan));
        rows.push_back(ftxui::paragraph("Generación actual " + std::to_string(frame.current_generation) +
            " · " + std::to_string(frame.current_alive) + " vivas · NEXT " + std::to_string(frame.next_alive)));
        rows.push_back(ftxui::paragraph(frame.engine_name + " · solicitados " + std::to_string(frame.requested_threads) +
            " · efectivos " + std::to_string(frame.effective_threads)));
        rows.push_back(ftxui::paragraph("Velocidad: " + speedLabel(frame.speed) + " · " + (frame.playing ? "Reproduciendo" : "En pausa")));
        Element body = didacticWorkers_ ? allWorkers(frame) : didacticBoards(snapshot, false);
        return panel("MODO DIDÁCTICO", ftxui::vbox({ftxui::vbox(std::move(rows)), std::move(body) | ftxui::flex}));
    }

    Element allWorkers(const DidacticFrame& frame) {
        std::vector<Element> rows;
        rows.push_back(heading("TRABAJO POR HILO · T tableros"));
        for (const auto& w : frame.workers) {
            std::ostringstream duration;
            duration << std::setprecision(6) << static_cast<double>(w.loop_duration_ns) / 1e6;
            const auto status = w.is_sequential ? "T0 único" : w.progress >= 1.0 ? "completo" : frame.playing ? "activo" : "pendiente";
            rows.push_back(ftxui::paragraph("T" + std::to_string(w.thread_id) + " · filas [" +
                std::to_string(w.first_row) + ", " + std::to_string(w.end_row) + ") · " +
                std::to_string(w.cells_processed) + " celdas · " + duration.str() + " ms · " +
                std::to_string(static_cast<int>(w.progress * 100)) + " % · " + status) |
                ftxui::color(colorsEnabled() ? workerColor(w.color_index) : ftxui::Color::Default));
        }
        if (frame.workers.empty()) rows.push_back(ftxui::text("Sin trabajadores registrados."));
        workerRow_ = std::clamp(workerRow_, 0, static_cast<int>(rows.size()) - 1);
        rows[static_cast<std::size_t>(workerRow_)] = rows[static_cast<std::size_t>(workerRow_)] | ftxui::focus;
        return ftxui::vbox(std::move(rows)) | ftxui::yframe | ftxui::flex;
    }

    Element didacticBoards(std::shared_ptr<const DidacticFrame> snapshot, bool paired) {
        return ui::allocatedElement([this, snapshot, paired](int width, int height) {
            const auto& frame = *snapshot;
            const int panelWidth = paired ? (width - 1) / 2 : width;
            // Each panel owns its title, separator, borders and two partition
            // rows. Reserve the viewport label before fitting the grid.
            if (height < (paired ? 10 : 6) || panelWidth < 4)
                return ftxui::text("Tablero sin espacio. T consulta hilos.");
            auto geom = computeDidacticBoardGeometry(std::max(0, panelWidth - (paired ? 2 : 0)),
                std::max(0, height - (paired ? 7 : 3)), frame.width, frame.height);
            didacticViewport_.clamp(frame.height, frame.width, geom.visible_rows, geom.visible_cols);
            geom.row_offset = didacticViewport_.row;
            geom.col_offset = didacticViewport_.column;
            auto board = [&](bool next) {
                auto grid = renderDidacticBoardPanel(frame, geom, next, colorsEnabled(), false);
                const auto title = std::string(next ? "NEXT · generación " : "CURRENT · generación ") +
                    std::to_string(next ? frame.next_generation : frame.current_generation);
                auto indicator = (next ? partitionIndicator(frame) : ftxui::text(" ")) |
                    ftxui::xframe | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN,
                        panelWidth - (paired ? 2 : 0));
                if (!paired) return ftxui::vbox({heading(title), std::move(indicator), std::move(grid) | ftxui::flex});
                return panel(title + (next ? (frame.fully_revealed ? " · REPRODUCCIÓN COMPLETA" :
                    frame.playing ? " · REPRODUCIENDO" : " · EN PAUSA") : ""),
                    didacticBoardBody(std::move(indicator), std::move(grid)));
            };
            Element boards = paired ? ftxui::hbox({board(false) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, panelWidth) | ftxui::flex,
                ftxui::text(" "), board(true) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, panelWidth) | ftxui::flex}) : board(didacticNext_);
            return ftxui::vbox({std::move(boards) | ftxui::flex,
                viewportLabel(didacticViewport_, frame.height, frame.width, geom.visible_rows, geom.visible_cols)});
        });
    }

    Element renderDidactic(std::size_t width, std::size_t height, bool compact) {
        if (!state_.didactic) {
            return panel("VISUALIZACIÓN DIDÁCTICA",
                ftxui::text("No hay un controlador activo."));
        }
        const auto snapshot = std::make_shared<const DidacticFrame>(state_.didactic->snapshot());
        const auto& frame = *snapshot;

        if (frame.mpi_trace || (state_.engine==Engine::MPI)) return renderMPIDidactic(snapshot);

        if (frame.is_preparing) {
            return panel("VISUALIZACIÓN DIDÁCTICA",
                ftxui::vbox({
                    ftxui::text("Preparando generación instrumentada...") |
                        ftxui::color(ftxui::Color::Yellow),
                    ftxui::text("La UI permanece receptiva."),
                }) | ftxui::flex);
        }
        if (!frame.error_message.empty() && frame.phase == DidacticController::Phase::Error) {
            return panel("VISUALIZACIÓN DIDÁCTICA",
                ftxui::vbox({
                    ftxui::text("Error en la generación instrumentada."),
                    ftxui::text(frame.error_message),
                }));
        }

        if (!frame.has_data) return panel("VISUALIZACIÓN DIDÁCTICA",ftxui::text("Sin datos. Pulsa R para reiniciar."));
        if (!ui::roomyTerminal(controls::terminalSize().columns, controls::terminalSize().rows))
            return renderCompactDidactic(snapshot);

        Element boards = didacticWorkers_ ? allWorkers(frame) : didacticBoards(snapshot, true);
        const bool colors = colorsEnabled();
        // Panel de trabajadores: sin flex, toma su tamano natural.
        // Asi no compite con los boards por el espacio vertical.
        Element workersPanel = didacticWorkers_ ? ftxui::text("") : renderDidacticWorkers(frame, colors);

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
            speedLine += "  ·  continuación automática; N etapa en pausa";
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
            std::move(workersPanel),
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
        if(state_.page==Page::Didactic && state_.engine==Engine::MPI && controls::terminalSize().columns<=44) return "V vista · [/] rank · ↑↓ · N paso · Esp pausa · +/- · R · Esc";
        if(state_.page==Page::Didactic && state_.engine==Engine::MPI) return "V vista · [/] rank · ↑↓ desplazar · N paso · Esp pausa · +/- · R · Esc";
        const auto terminal = controls::terminalSize();
        if (!ui::roomyTerminal(terminal.columns, terminal.rows)) {
            switch (state_.page) {
                case Page::Simulation: return std::string(simulationSingle_ ? "V vista · " : "") + "Flechas mover · N paso · Esp pausa · R · Esc";
                case Page::Patterns: return catalogDetail_
                    ? "D lista · V vista · Flechas tablero · PgUp/PgDn datos · Enter · M · Esc"
                    : "↑↓ patrones · D detalle · Enter · M ruta · Esc";
                case Page::Didactic: return didacticWorkers_
                    ? "T tableros · ↑↓/PgUp/PgDn hilos · N etapa · Esp · R · +/- · Esc"
                    : "V vista · T hilos · Flechas tablero · N etapa · Esp · R · +/- · Esc";
                case Page::ComparisonConfig:
                case Page::BenchmarkScaling: return "Tab/Mayús+Tab campos · Enter · Esc";
                case Page::ExperimentExecution:
                case Page::BenchmarkResults: return "←→ vistas · ↑↓ desplazar · Esc volver";
                default: break;
            }
        }
        switch (state_.page) {
            case Page::Main: return "↑↓ navegar · Enter seleccionar · C patrones · 0 salir";
            case Page::Patterns: return catalogDetail_
                ? "D lista · Flechas tablero · PgUp/PgDn datos · Enter · M ruta · Esc"
                : "↑↓ patrones · D detalle · Enter cargar · M ruta · Esc volver";
            case Page::Engines: return "↑↓ navegar · Enter · S/O/M motor · C procesos MPI · Esc volver";
            case Page::Threads: return "↑↓ navegar · Enter seleccionar · Esc volver";
            case Page::PathInput:
            case Page::MPIInput:
            case Page::ThreadInput: return "Enter confirmar · Esc cancelar";
            case Page::Simulation: return "Flechas mover · N paso · Espacio/P pausa/reanuda · R reiniciar · U recuperar · Esc menú · Q abandonar";
            case Page::BenchmarkMenu: return "↑↓ navegar · Enter seleccionar · Esc menú principal";
            case Page::ExperimentExecution: return (showingComparison() || showingScaling())
                ? "←→ Resumen/Muestras/Método · ↑↓ desplazar · Esc configuración"
                : "↑↓ desplazar · Esc configuración";
            case Page::ComparisonConfig:
            case Page::BenchmarkScaling: return "Tab/Mayús+Tab campos · Enter activar · Esc volver a Benchmark";
            case Page::BenchmarkResults: return (showingComparison() || showingScaling())
                ? "←→ Resumen/Muestras/Método · ↑↓ desplazar · Esc Benchmark"
                : "↑↓ desplazar · Esc volver a Benchmark";
            case Page::Benchmark: return "Esc volver a Benchmark · estados equivalentes";
            case Page::Didactic: return "T tableros/hilos · Flechas mover · N etapa en pausa · Esp pausa · R · +/- · Esc";
        }
        return {};
    }

    ftxui::ScreenInteractive& screen_;
    DifferentialOutputBuffer& output_;
    InteractiveState state_;
    std::vector<std::string> mainItems_;
    std::vector<std::string> benchmarkItems_;
    std::vector<std::string> patternItems_;
    std::vector<std::string> patternActionItems_;
    std::vector<std::string> engineItems_;
    std::vector<std::string> threadItems_;
    PatternCatalog catalog_;
    PatternPreview catalogPreview_;
    Page pathReturnPage_{Page::Main};
    Page destinationAfterPath_{Page::Main};
    std::atomic<unsigned> pendingEvents_{0};
    std::jthread playbackTimer_;
    std::mutex benchmarkMutex_;
    std::optional<BenchmarkResult> benchmarkResult_;
    std::string benchmarkError_;
    std::thread benchmarkWorker_;
    std::unique_ptr<ExperimentExecution> experiment_;
    bool exitWhenExperimentFinishes_{false};
    bool exitAfterSimulation_{false}, replaceSimulation_{false};
    int resultRow_{0}, resultRows_{1};
    int comparisonTab_{0};
    int scalingTab_{0};
    bool simulationSingle_{false};
    int workerRow_{0};
    bool simulationNext_{false}, catalogNext_{false}, catalogDetail_{false};
    int mpiDidacticView_=0, mpiDidacticRank_=0, mpiDidacticRow_=0, mpiDidacticMaxOffset_=0;
    bool didacticNext_{true}, didacticWorkers_{false};
    std::chrono::steady_clock::time_point didacticTickAt_{};
    int detailRow_{0};
    ui::BoardViewport simulationViewport_, catalogViewport_, didacticViewport_;
    ftxui::Component comparisonForm_, scalingForm_;
    ftxui::Component mainMenu_;
    ftxui::Component benchmarkMenu_;
    ftxui::Component patternMenu_;
    ftxui::Component patternActionsMenu_;
    ftxui::Component engineMenu_;
    ftxui::Component threadMenu_;
    ftxui::Component pathInput_;
    ftxui::Component threadInput_;
    ftxui::Component mpiInput_;
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

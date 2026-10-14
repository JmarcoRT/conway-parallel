#include "ui/simulation_layout.hpp"
#include "ui/terminal_text.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace conway::ui {

namespace {

std::string line(const std::string& text, std::size_t width, bool colors, bool centered = false) {
    if (width < 3) {
        return terminal_text::truncate(text, width);
    }
    return std::string(colors ? color::WHITE : "") + "│" +
        (colors ? color::RESET : "") + terminal_text::fit(text, width - 2, centered) +
        (colors ? color::WHITE : "") + "│" + (colors ? color::RESET : "");
}

std::string rule(
    std::size_t width,
    bool colors,
    const std::string& left,
    const std::string& right
) {
    if (width < 3) {
        return std::string(width, '-');
    }
    std::string output = colors ? color::WHITE : "";
    output += left;
    for (std::size_t column = 0; column < width - 2; ++column) {
        output += "─";
    }
    output += right;
    output += colors ? color::RESET : "";
    return output;
}

std::vector<std::string> panelLines(
    const BoardPanelData& data,
    std::size_t width,
    std::size_t height,
    bool colors
) {
    std::vector<std::string> output;
    output.reserve(height + 4);
    if (width < 3) {
        output.push_back(terminal_text::truncate(data.title, width));
        for (std::size_t row = 0; row < height; ++row) {
            const std::size_t visibleRows = std::min(height, data.lines.size());
            const std::size_t topPadding = (height - visibleRows) / 2;
            const bool hasContent = row >= topPadding && row - topPadding < visibleRows;
            output.push_back(hasContent
                ? terminal_text::truncate(data.lines[row - topPadding], width)
                : "");
        }
        return output;
    }

    const std::size_t visibleRows = std::min(height, data.lines.size());
    const std::size_t topPadding = (height - visibleRows) / 2;
    output.push_back(rule(width, colors, "┌", "┐"));
    output.push_back(line(data.title, width, colors, true));
    output.push_back(rule(width, colors, "├", "┤"));
    for (std::size_t row = 0; row < height; ++row) {
        const bool hasContent = row >= topPadding && row - topPadding < visibleRows;
        const std::string content = hasContent ? data.lines[row - topPadding] : "";
        output.push_back(line(content, width, colors, true));
    }
    output.push_back(rule(width, colors, "└", "┘"));
    return output;
}

std::size_t preferredPanelWidth(const BoardPanelData& data, std::size_t maximum) {
    std::size_t contentWidth = terminal_text::visibleWidth(data.title);
    for (const auto& text : data.lines) {
        contentWidth = std::max(contentWidth, terminal_text::visibleWidth(text));
    }
    return std::min(maximum, contentWidth + 4);
}

std::vector<std::string> renderRows(
    const SimulationHeaderInfo& header,
    const ExecutionInfo& execution,
    const BoardPanelData& currentPanel,
    const BoardPanelData& nextPanel,
    const SimulationLayoutConfig& config
) {
    const std::size_t width = std::max<std::size_t>(1, config.width);
    const bool colors = config.colorEnabled;
    const std::size_t inner = width > 2 ? width - 2 : width;
    const bool sideBySide = inner >= 84;
    const std::size_t panelHeight = std::min(config.innerPanelHeight, std::size_t{100});
    std::vector<std::string> rows;

    if (width < 86 || config.terminalHeight < 18) {
        const auto isViewportNotice = [](const std::string& text) {
            return text.rfind("Viewport:", 0) == 0;
        };
        const std::size_t maxBoardRows = std::max<std::size_t>(
            1, config.terminalHeight > 6 ? (config.terminalHeight - 6) / 2 : 1);
        std::vector<std::string> compact;
        auto appendLimited = [&](const BoardPanelData& data) {
            const std::size_t availableBoardRows = static_cast<std::size_t>(std::count_if(
                data.lines.begin(), data.lines.end(),
                [&](const std::string& text) { return !isViewportNotice(text); }));
            const bool partial = availableBoardRows > maxBoardRows ||
                std::any_of(data.lines.begin(), data.lines.end(), isViewportNotice);
            compact.push_back(data.title + (partial ? "  (vista parcial)" : ""));
            std::size_t included = 0;
            for (const auto& boardLine : data.lines) {
                if (isViewportNotice(boardLine)) {
                    continue;
                }
                if (included == maxBoardRows) {
                    break;
                }
                compact.push_back(terminal_text::truncate(boardLine, width));
                ++included;
            }
        };
        compact.push_back("JUEGO DE LA VIDA | Patrón: " + header.filename + " (" +
            std::to_string(header.boardWidth) + " × " + std::to_string(header.boardHeight) +
            ") | Motor: " + header.mode);
        compact.push_back("Generación: " + std::to_string(execution.generation) +
            "  |  Vivas: " + std::to_string(execution.aliveCount) +
            "  |  Estado: " + header.statusText);
        appendLimited(currentPanel);
        appendLimited(nextPanel);
        compact.push_back(execution.workersInfo);
        compact.push_back("Espacio Pausa  N Paso  R Reiniciar  Esc Menú");
        return compact;
    }

    rows.push_back(rule(width, colors, "┌", "┐"));
    rows.push_back(line(colors ? std::string(color::CYAN) + color::BOLD +
        "JUEGO DE LA VIDA  ·  SIMULACIÓN" + color::RESET : "JUEGO DE LA VIDA  ·  SIMULACIÓN",
        width, colors, true));
    rows.push_back(line("Patrón: " + header.filename + " (" +
        std::to_string(header.boardWidth) + " × " + std::to_string(header.boardHeight) +
        ")  |  Motor: " + header.mode, width, colors, true));
    rows.push_back(rule(width, colors, "├", "┤"));

    if (sideBySide) {
        const std::size_t panelWidth = std::max<std::size_t>(
            3, std::min(preferredPanelWidth(currentPanel, (inner - 4) / 2),
                        preferredPanelWidth(nextPanel, (inner - 4) / 2)));
        const auto left = panelLines(currentPanel, panelWidth, panelHeight, colors);
        const auto right = panelLines(nextPanel, panelWidth, panelHeight, colors);
        for (std::size_t index = 0; index < std::min(left.size(), right.size()); ++index) {
            rows.push_back(terminal_text::fit(left[index] + "    " + right[index], width, true));
        }
    } else {
        const std::size_t panelWidth = std::max<std::size_t>(
            3, std::min(preferredPanelWidth(currentPanel, inner),
                        preferredPanelWidth(nextPanel, inner)));
        const auto current = panelLines(currentPanel, panelWidth, panelHeight, colors);
        for (const auto& panelLine : current) {
            rows.push_back(terminal_text::fit(panelLine, width, true));
        }
        const auto next = panelLines(nextPanel, panelWidth, panelHeight, colors);
        for (const auto& panelLine : next) {
            rows.push_back(terminal_text::fit(panelLine, width, true));
        }
    }

    rows.push_back(rule(width, colors, "├", "┤"));
    rows.push_back(line("Generación: " + std::to_string(execution.generation) +
        "  |  Vivas: " + std::to_string(execution.aliveCount) +
        "  |  Estado: " + header.statusText, width, colors, true));
    rows.push_back(line(execution.workersInfo, width, colors, true));
    rows.push_back(line("Viva: bloque sólido    Muerta: trama    Espacio Pausa/Reanudar    N Paso    R Reiniciar    Esc Menú",
        width, colors, true));
    rows.push_back(rule(width, colors, "└", "┘"));
    return rows;
}

} // namespace

void renderSimulationLayout(
    const SimulationHeaderInfo& header,
    const ExecutionInfo& execution,
    const BoardPanelData& currentPanel,
    const BoardPanelData& nextPanel,
    const SimulationLayoutConfig& config,
    std::ostream& out
) {
    const std::size_t width = std::max<std::size_t>(1, config.width);
    const std::size_t height = std::max<std::size_t>(1, config.terminalHeight);
    auto rows = renderRows(header, execution, currentPanel, nextPanel, config);
    if (rows.size() > height) {
        rows.resize(height);
    }
    const std::size_t verticalPadding = height - rows.size();
    const std::size_t topPadding = verticalPadding / 2;
    for (std::size_t index = 0; index < topPadding; ++index) {
        out << '\n';
    }
    for (const auto& row : rows) {
        out << terminal_text::fit(row, width, true) << '\n';
    }
    for (std::size_t index = topPadding + rows.size(); index < height; ++index) {
        out << '\n';
    }
}

void renderSimulationLayout(
    const SimulationHeaderInfo& header,
    const ExecutionInfo& execution,
    const BoardPanelData& currentPanel,
    const BoardPanelData& nextPanel,
    const SimulationLayoutConfig& config
) {
    renderSimulationLayout(header, execution, currentPanel, nextPanel, config, std::cout);
}

} // namespace conway::ui

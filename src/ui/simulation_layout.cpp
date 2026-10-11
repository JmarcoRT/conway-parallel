#include "ui/simulation_layout.hpp"

#include <algorithm>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>

namespace conway::ui {

namespace {

std::string repeat(const std::string& text, std::size_t count) {
    std::string result;
    result.reserve(text.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        result += text;
    }
    return result;
}

std::size_t visualLength(const std::string& text) {
    std::size_t length = 0;
    bool escape = false;
    for (const unsigned char character : text) {
        if (escape) {
            if ((character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z')) {
                escape = false;
            }
        } else if (character == 0x1b) {
            escape = true;
        } else if ((character & 0xc0) != 0x80) {
            ++length;
        }
    }
    return length;
}

std::string truncate(const std::string& text, std::size_t width) {
    if (visualLength(text) <= width) {
        return text;
    }
    if (width == 0) {
        return {};
    }

    std::string result;
    std::size_t visible = 0;
    bool escape = false;
    for (const unsigned char character : text) {
        if (escape) {
            result += static_cast<char>(character);
            if ((character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z')) {
                escape = false;
            }
        } else if (character == 0x1b) {
            escape = true;
            result += static_cast<char>(character);
        } else {
            if ((character & 0xc0) != 0x80) {
                if (visible == width - 1) {
                    break;
                }
                ++visible;
            }
            result += static_cast<char>(character);
        }
    }
    if (escape) {
        result += color::RESET;
    }
    result += "…";
    return result;
}

std::string fit(const std::string& text, std::size_t width, bool center = false) {
    const std::string clipped = truncate(text, width);
    const std::size_t length = visualLength(clipped);
    const std::size_t padding = width > length ? width - length : 0;
    const std::size_t left = center ? padding / 2 : 0;
    return repeat(" ", left) + clipped + repeat(" ", padding - left);
}

void frameLine(std::ostream& out, const std::string& text, std::size_t width, bool colors) {
    out << (colors ? color::WHITE : "") << "│" << (colors ? color::RESET : "")
        << fit(text, width)
        << (colors ? color::WHITE : "") << "│" << (colors ? color::RESET : "") << '\n';
}

void frameWrapped(std::ostream& out, const std::string& text, std::size_t width, bool colors) {
    std::istringstream words(text);
    std::string word;
    std::string line;
    while (words >> word) {
        if (!line.empty() && visualLength(line) + 1 + visualLength(word) > width) {
            frameLine(out, line, width, colors);
            line.clear();
        }
        if (!line.empty()) {
            line += ' ';
        }
        line += word;
    }
    frameLine(out, line, width, colors);
}

void rule(std::ostream& out, std::size_t width, bool colors, bool middle = true) {
    const std::string edge = middle ? "├" : "┌";
    const std::string end = middle ? "┤" : "┐";
    out << (colors ? color::WHITE : "") << edge << repeat("─", width) << end
        << (colors ? color::RESET : "") << '\n';
}

void panel(
    std::ostream& out,
    const BoardPanelData& data,
    std::size_t width,
    std::size_t height,
    bool colors
) {
    if (width < 4) {
        frameLine(out, data.title, width, colors);
        return;
    }
    const std::size_t inside = width - 2;
    out << (colors ? color::GRAY : "") << "┌" << repeat("─", inside) << "┐"
        << (colors ? color::RESET : "") << '\n';
    frameLine(out, data.title, inside, colors);
    out << (colors ? color::GRAY : "") << "├" << repeat("─", inside) << "┤"
        << (colors ? color::RESET : "") << '\n';
    for (std::size_t line = 0; line < height; ++line) {
        const std::string content = line < data.lines.size() ? data.lines[line] : "";
        frameLine(out, content, inside, colors);
    }
    out << (colors ? color::GRAY : "") << "└" << repeat("─", inside) << "┘"
        << (colors ? color::RESET : "") << '\n';
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
    const std::size_t frameWidth = std::min(config.width, std::size_t{240});
    if (frameWidth < 3) {
        if (frameWidth == 0) {
            return;
        }
        out << truncate(" CONWAY | SIMULACIÓN ", frameWidth) << '\n'
            << truncate("Motor: " + header.mode + " | Estado: " + header.statusText,
                        frameWidth) << '\n'
            << truncate("Generación: " + std::to_string(execution.generation) +
                            " | Vivas: " + std::to_string(execution.aliveCount),
                        frameWidth) << '\n';
        return;
    }
    const std::size_t inside = frameWidth - 2;
    const std::size_t panelHeight = std::min(config.innerPanelHeight, std::size_t{40});
    const bool colors = config.colorEnabled;

    out << (colors ? color::WHITE : "") << "┌" << repeat("─", inside) << "┐"
        << (colors ? color::RESET : "") << '\n';
    frameLine(out, colors ? std::string(color::CYAN) + color::BOLD +
        " CONWAY | SIMULACIÓN " + color::RESET : " CONWAY | SIMULACIÓN ", inside, colors);
    frameLine(out, "Motor: " + header.mode + " | Estado: " + header.statusText, inside, colors);
    frameLine(out, "Patrón: " + header.filename + " (" +
        std::to_string(header.boardWidth) + " x " + std::to_string(header.boardHeight) + ")",
        inside, colors);
    rule(out, inside, colors);

    const std::size_t panelWidth = inside >= 70 ? (inside - 4) / 2 : inside;
    const bool sideBySide = inside >= 70;
    if (sideBySide) {
        const std::size_t leftWidth = panelWidth;
        const std::size_t rightWidth = inside - leftWidth - 4;
        const std::size_t boardHeight = std::max(
            std::min(currentPanel.lines.size(), panelHeight),
            std::min(nextPanel.lines.size(), panelHeight));
        std::ostringstream left;
        std::ostringstream right;
        panel(left, currentPanel, leftWidth, boardHeight, colors);
        panel(right, nextPanel, rightWidth, boardHeight, colors);
        std::istringstream leftLines(left.str());
        std::istringstream rightLines(right.str());
        std::string leftLine;
        std::string rightLine;
        while (std::getline(leftLines, leftLine) && std::getline(rightLines, rightLine)) {
            out << fit(leftLine, leftWidth) << "    " << fit(rightLine, rightWidth) << '\n';
        }
    } else {
        panel(out, currentPanel, panelWidth, panelHeight, colors);
        panel(out, nextPanel, panelWidth, panelHeight, colors);
    }

    rule(out, inside, colors);
    frameLine(out, "Generación: " + std::to_string(execution.generation) +
        " | Vivas: " + std::to_string(execution.aliveCount) +
        " | " + execution.stepProgress, inside, colors);
    frameWrapped(out, execution.workersInfo, inside, colors);
    frameLine(out, "[Espacio] Pausa/Reanudar  [N] Paso  [R] Reiniciar  [Esc] Menú",
        inside, colors);
    out << (colors ? color::WHITE : "") << "└" << repeat("─", inside) << "┘"
        << (colors ? color::RESET : "") << '\n';
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

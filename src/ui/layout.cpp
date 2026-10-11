#include "ui/layout.hpp"
#include "ui/terminal_text.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace conway::ui {

namespace {

std::string frame(const std::string& text, std::size_t width, bool centered, bool colors) {
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

std::vector<std::string> visibleOptions(
    const std::vector<MenuItem>& options,
    std::size_t selected,
    std::size_t limit,
    bool colors
) {
    std::vector<std::string> lines;
    if (options.empty() || limit == 0) {
        return lines;
    }
    selected = std::min(selected, options.size() - 1);
    const std::size_t itemLimit = limit > 2 ? limit - 2 : 1;
    const std::size_t start = selected >= itemLimit ? selected - itemLimit + 1 : 0;
    const std::size_t end = std::min(options.size(), start + itemLimit);
    if (start > 0) {
        lines.emplace_back("…");
    }
    for (std::size_t index = start; index < end; ++index) {
        const bool active = index == selected;
        const auto& option = options[index];
        const std::string prefix = active ? "› " : "  ";
        std::string value = prefix + "[" + option.key + "]  " + option.label;
        if (active && colors) {
            value = std::string(color::CYAN) + color::BOLD + prefix +
                "[" + option.key + "]" + color::RESET + color::WHITE +
                color::BOLD + "  " + option.label + color::RESET;
        }
        lines.push_back(std::move(value));
    }
    if (end < options.size()) {
        lines.emplace_back("…");
    }
    return lines;
}

} // namespace

void renderLayout(
    const std::string& title,
    const std::string& subtitle,
    const ActiveInputInfo& inputInfo,
    const std::vector<MenuItem>& options,
    std::size_t selectedIndex,
    const LayoutConfig& config,
    const std::string& notice
) {
    const std::size_t width = std::max<std::size_t>(1, config.width);
    const std::size_t height = std::max<std::size_t>(1, config.height);
    const bool colors = config.colorEnabled;
    std::vector<std::string> content;

    if (width < 24 || height < 12) {
        content.push_back(colors ? std::string(color::CYAN) + color::BOLD + title + color::RESET : title);
        if (!subtitle.empty()) {
            content.push_back(subtitle);
        }
        if (!notice.empty()) {
            content.push_back(notice);
        }
        content.push_back(inputInfo.hasInput
            ? "Actual: " + inputInfo.filename + " (" + std::to_string(inputInfo.width) +
                " x " + std::to_string(inputInfo.height) + ")"
            : "Patrón: ninguno");
        const std::size_t optionLimit = height > content.size() + 1
            ? height - content.size() - 1
            : 1;
        auto optionsLines = visibleOptions(options, selectedIndex, optionLimit, colors);
        content.insert(content.end(), optionsLines.begin(), optionsLines.end());
        content.push_back("↑/↓ mover  Enter elegir  Esc volver");
    } else {
        content.push_back(rule(width, colors, "┌", "┐"));
        content.push_back(frame("", width, false, colors));
        const std::string heading = colors
            ? std::string(color::CYAN) + color::BOLD + title + color::RESET
            : title;
        content.push_back(frame(heading, width, true, colors));
        if (!subtitle.empty()) {
            content.push_back(frame(colors
                ? std::string(color::GRAY) + subtitle + color::RESET
                : subtitle, width, true, colors));
        }
        content.push_back(frame(inputInfo.hasInput
            ? "Patrón actual: " + inputInfo.filename + "  ·  " +
                std::to_string(inputInfo.width) + " × " + std::to_string(inputInfo.height) +
                "  ·  " + std::to_string(inputInfo.aliveCells) + " vivas"
            : "Patrón actual: ninguno", width, true, colors));
        if (!notice.empty()) {
            const bool isError = notice.rfind("Error", 0) == 0;
            content.push_back(frame(colors
                ? std::string(isError ? "\033[1;31m" : color::GREEN) +
                    notice + color::RESET
                : notice, width, true, colors));
        }
        content.push_back(rule(width, colors, "├", "┤"));

        const std::size_t fixedRows = content.size() + 4;
        const std::size_t availableRows = height > fixedRows
            ? height - fixedRows
            : 1;
        const std::size_t visibleOptionCount = std::min(
            options.size(), std::max<std::size_t>(1, availableRows));
        const std::size_t optionSpacing = visibleOptionCount > 1
            ? std::min<std::size_t>(
                2, (availableRows - visibleOptionCount) / (visibleOptionCount + 1))
            : 0;
        const std::size_t optionLimit = availableRows -
            optionSpacing * (visibleOptionCount > 0 ? visibleOptionCount - 1 : 0);
        const auto optionsLines = visibleOptions(options, selectedIndex, optionLimit, colors);
        for (std::size_t index = 0; index < optionsLines.size(); ++index) {
            content.push_back(frame(optionsLines[index], width, true, colors));
            if (index + 1 < optionsLines.size()) {
                for (std::size_t blank = 0; blank < optionSpacing; ++blank) {
                    content.push_back(frame("", width, false, colors));
                }
            }
        }
        content.push_back(rule(width, colors, "├", "┤"));
        content.push_back(frame("↑ ↓ Navegar    Enter Seleccionar    Esc Volver", width, true, colors));
        content.push_back(rule(width, colors, "└", "┘"));
    }

    for (auto& line : content) {
        line = terminal_text::fit(line, width, true);
    }
    if (content.size() > height) {
        content.resize(height);
    }
    const std::size_t verticalPadding = height - content.size();
    const std::size_t topPadding = verticalPadding / 2;
    for (std::size_t row = 0; row < topPadding; ++row) {
        std::cout << '\n';
    }
    for (const auto& line : content) {
        std::cout << line << '\n';
    }
    for (std::size_t row = topPadding + content.size(); row < height; ++row) {
        std::cout << '\n';
    }
}

void renderScreen(
    const std::string& title,
    const std::string& subtitle,
    const std::vector<std::string>& body,
    const std::string& footer,
    const LayoutConfig& config
) {
    const std::size_t width = std::max<std::size_t>(1, config.width);
    const std::size_t height = std::max<std::size_t>(1, config.height);
    const bool colors = config.colorEnabled;
    std::vector<std::string> lines;
    if (width >= 24 && height >= 10) {
        lines.push_back(rule(width, colors, "┌", "┐"));
        lines.push_back(frame(colors
            ? std::string(color::CYAN) + color::BOLD + title + color::RESET
            : title, width, true, colors));
        if (!subtitle.empty()) {
            lines.push_back(frame(subtitle, width, true, colors));
        }
        lines.push_back(rule(width, colors, "├", "┤"));
        const std::size_t bodyLimit = height > lines.size() + 4
            ? height - lines.size() - 4
            : 1;
        for (std::size_t index = 0; index < std::min(body.size(), bodyLimit); ++index) {
            lines.push_back(frame(body[index], width, true, colors));
        }
        lines.push_back(rule(width, colors, "├", "┤"));
        lines.push_back(frame(footer, width, true, colors));
        lines.push_back(rule(width, colors, "└", "┘"));
    } else {
        lines.push_back(terminal_text::truncate(title, width));
        if (!subtitle.empty()) {
            lines.push_back(terminal_text::truncate(subtitle, width));
        }
        for (const auto& line : body) {
            lines.push_back(terminal_text::truncate(line, width));
        }
        lines.push_back(terminal_text::truncate(footer, width));
    }

    if (lines.size() > height) {
        lines.resize(height);
    }
    const std::size_t padding = height - lines.size();
    const std::size_t top = padding / 2;
    for (std::size_t row = 0; row < top; ++row) {
        std::cout << '\n';
    }
    for (const auto& line : lines) {
        std::cout << terminal_text::fit(line, width, true) << '\n';
    }
    for (std::size_t row = top + lines.size(); row < height; ++row) {
        std::cout << '\n';
    }
}

} // namespace conway::ui

#include "ui/layout.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

namespace conway::ui {

namespace {

// Helper para repetir una cadena N veces
std::string repeatStr(const std::string& str, std::size_t times) {
    std::string result;
    result.reserve(str.size() * times);
    for (std::size_t i = 0; i < times; ++i) {
        result += str;
    }
    return result;
}

// Helper para calcular longitud visual en caracteres UTF-8 (1 codepoint = 1 celda de terminal)
std::size_t utf8Length(const std::string& str) {
    std::size_t len = 0;
    for (std::size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if ((c & 0xC0) != 0x80) {
            len++;
        }
    }
    return len;
}

/**
 * Imprime una línea dentro del cuadro exterior garantizando matemáticamente
 * que el espacio interior entre los bordes '│' es exactamente 'innerWidth' caracteres.
 */
void printFrameLine(
    const std::string& rawText,
    const std::string& formattedText,
    std::size_t innerWidth,
    std::size_t leftMargin = 0,
    bool center = false
) {
    std::size_t textVisualLen = utf8Length(rawText);
    std::size_t leftPadding = 0;
    std::size_t rightPadding = 0;

    if (textVisualLen >= innerWidth) {
        leftPadding = 0;
        rightPadding = 0;
    } else if (center) {
        leftPadding = (innerWidth - textVisualLen) / 2;
        rightPadding = innerWidth - textVisualLen - leftPadding;
    } else {
        leftPadding = std::min(leftMargin, innerWidth - textVisualLen);
        rightPadding = innerWidth - textVisualLen - leftPadding;
    }

    std::cout << color::WHITE << "│" << color::RESET
              << repeatStr(" ", leftPadding)
              << formattedText
              << repeatStr(" ", rightPadding)
              << color::WHITE << "│" << color::RESET << "\n";
}

// Imprime una línea vacía de la caja
void printEmptyLine(std::size_t innerWidth) {
    printFrameLine("", "", innerWidth, 0, false);
}

} // namespace

void renderLayout(
    const std::string& title,
    const std::string& subtitle,
    const ActiveInputInfo& inputInfo,
    const std::vector<MenuItem>& options,
    std::size_t selectedIndex,
    const LayoutConfig& config
) {
    const std::size_t frameWidth = std::max<std::size_t>(config.width, 60);
    const std::size_t innerWidth = frameWidth - 2;

    // 1. Borde superior del marco exterior: ┌────────────────────┐
    std::cout << color::WHITE << "┌" << repeatStr("─", innerWidth) << "┐" << color::RESET << "\n";

    printEmptyLine(innerWidth);

    // 2. Título centrado entre separadores: ──── TITULO ────
    std::string titleRaw = "──── " + title + " ────";
    std::ostringstream titleFormatted;
    titleFormatted << color::CYAN << color::BOLD << "──── " << title << " ────" << color::RESET;
    printFrameLine(titleRaw, titleFormatted.str(), innerWidth, 0, true);

    // Subtítulo (opcional)
    if (!subtitle.empty()) {
        std::ostringstream subFormatted;
        subFormatted << color::GRAY << subtitle << color::RESET;
        printFrameLine(subtitle, subFormatted.str(), innerWidth, 0, true);
    }

    printEmptyLine(innerWidth);

    // 3. Línea de estado de la entrada activa
    std::string inputStatusRaw = "Entrada activa: ";
    std::ostringstream inputStatusFormatted;
    inputStatusFormatted << color::WHITE << "Entrada activa: " << color::RESET;

    if (inputInfo.hasInput) {
        std::ostringstream val;
        val << inputInfo.filename << " (" << inputInfo.width << " x " << inputInfo.height << ")";
        inputStatusRaw += val.str();
        inputStatusFormatted << color::GREEN << val.str() << color::RESET;
    } else {
        inputStatusRaw += "ninguna";
        inputStatusFormatted << color::GREEN << "ninguna" << color::RESET;
    }

    printFrameLine(inputStatusRaw, inputStatusFormatted.str(), innerWidth, 3, false);

    // Separador horizontal superior: ├────────────────────┤
    std::cout << color::WHITE << "├" << repeatStr("─", innerWidth) << "┤" << color::RESET << "\n";

    printEmptyLine(innerWidth);

    // 4. Zona de opciones
    for (std::size_t i = 0; i < options.size(); ++i) {
        bool isSelected = (i == selectedIndex);
        const auto& opt = options[i];

        // Texto base de la opción: [1]  Benchmark
        std::string optTextRaw = "[" + opt.key + "]  " + opt.label;

        if (isSelected) {
            std::size_t textVisualLen = utf8Length(optTextRaw);
            std::size_t boxInnerWidth = textVisualLen + 4; // 2 espacios a cada lado

            std::string topBoxRaw = "┌" + repeatStr("─", boxInnerWidth) + "┐";
            std::ostringstream topBoxFormatted;
            topBoxFormatted << color::GRAY << topBoxRaw << color::RESET;
            printFrameLine(topBoxRaw, topBoxFormatted.str(), innerWidth, 0, true);

            std::string midBoxRaw = "│  " + optTextRaw + "  │";
            std::ostringstream midBoxFormatted;
            midBoxFormatted << color::GRAY << "│  " << color::RESET
                            << color::CYAN << color::BOLD << "[" << opt.key << "]" << color::RESET
                            << color::WHITE << color::BOLD << "  " << opt.label << color::RESET
                            << color::GRAY << "  │" << color::RESET;
            printFrameLine(midBoxRaw, midBoxFormatted.str(), innerWidth, 0, true);

            std::string botBoxRaw = "└" + repeatStr("─", boxInnerWidth) + "┘";
            std::ostringstream botBoxFormatted;
            botBoxFormatted << color::GRAY << botBoxRaw << color::RESET;
            printFrameLine(botBoxRaw, botBoxFormatted.str(), innerWidth, 0, true);
        } else {
            std::ostringstream optFormatted;
            optFormatted << color::CYAN << "[" << opt.key << "]" << color::RESET
                         << color::WHITE << "  " << opt.label << color::RESET;
            printFrameLine(optTextRaw, optFormatted.str(), innerWidth, 0, true);
        }

        if (i + 1 < options.size()) {
            printEmptyLine(innerWidth);
        }
    }

    printEmptyLine(innerWidth);

    // Separador horizontal inferior: ├────────────────────┤
    std::cout << color::WHITE << "├" << repeatStr("─", innerWidth) << "┤" << color::RESET << "\n";

    // 5. Footer de ayuda de navegación fijo
    std::string footerRaw = "↑ ↓ Navegar   Enter Seleccionar";
    std::ostringstream footerFormatted;
    footerFormatted << color::GRAY << "↑ ↓ Navegar   Enter Seleccionar" << color::RESET;
    printFrameLine(footerRaw, footerFormatted.str(), innerWidth, 0, true);

    // Borde inferior del marco exterior: └────────────────────┘
    std::cout << color::WHITE << "└" << repeatStr("─", innerWidth) << "┘" << color::RESET << "\n";
}

} // namespace conway::ui

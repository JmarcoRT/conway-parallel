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

// Helper para calcular longitud visual en caracteres UTF-8
std::size_t utf8Length(const std::string& str) {
    std::size_t len = 0;
    for (std::size_t i = 0; i < str.size(); ++i) {
        if ((static_cast<unsigned char>(str[i]) & 0xC0) != 0x80) {
            len++;
        }
    }
    return len;
}

// Imprime una línea centrada dentro de la caja con bordes laterales '│'
void printCenteredLine(const std::string& rawText, const std::string& formattedText, std::size_t innerWidth) {
    std::size_t textLen = utf8Length(rawText);
    std::size_t leftPadding = (innerWidth > textLen) ? (innerWidth - textLen) / 2 : 0;
    std::size_t rightPadding = innerWidth - textLen - leftPadding;

    std::cout << color::WHITE << "│" << color::RESET;
    std::cout << repeatStr(" ", leftPadding);
    std::cout << formattedText;
    std::cout << repeatStr(" ", rightPadding);
    std::cout << color::WHITE << "│" << color::RESET << "\n";
}

// Imprime una línea vacía dentro de la caja
void printEmptyLine(std::size_t innerWidth) {
    std::cout << color::WHITE << "│" << color::RESET
              << repeatStr(" ", innerWidth)
              << color::WHITE << "│" << color::RESET << "\n";
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
    printCenteredLine(titleRaw, titleFormatted.str(), innerWidth);

    // Subtítulo (opcional)
    if (!subtitle.empty()) {
        std::ostringstream subFormatted;
        subFormatted << color::GRAY << subtitle << color::RESET;
        printCenteredLine(subtitle, subFormatted.str(), innerWidth);
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

    // Alinear entrada activa a la izquierda con padding de 3 espacios
    std::size_t leftMargin = 3;
    std::size_t statusLen = utf8Length(inputStatusRaw);
    std::size_t rightPadding = (innerWidth > leftMargin + statusLen) ? innerWidth - leftMargin - statusLen : 0;

    std::cout << color::WHITE << "│" << color::RESET;
    std::cout << repeatStr(" ", leftMargin) << inputStatusFormatted.str() << repeatStr(" ", rightPadding);
    std::cout << color::WHITE << "│" << color::RESET << "\n";

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
            // Usar utf8Length para medir adecuadamente caracteres con acento como 'ó'
            std::size_t textVisualLen = utf8Length(optTextRaw);
            std::size_t boxInnerWidth = textVisualLen + 4; // 2 espacios por lado

            std::string topBoxRaw = "┌" + repeatStr("─", boxInnerWidth) + "┐";
            std::ostringstream topBoxFormatted;
            topBoxFormatted << color::GRAY << topBoxRaw << color::RESET;
            printCenteredLine(topBoxRaw, topBoxFormatted.str(), innerWidth);

            std::string midBoxRaw = "│  " + optTextRaw + "  │";
            std::ostringstream midBoxFormatted;
            midBoxFormatted << color::GRAY << "│  " << color::RESET
                            << color::CYAN << color::BOLD << "[" << opt.key << "]" << color::RESET
                            << color::WHITE << color::BOLD << "  " << opt.label << color::RESET
                            << color::GRAY << "  │" << color::RESET;
            printCenteredLine(midBoxRaw, midBoxFormatted.str(), innerWidth);

            std::string botBoxRaw = "└" + repeatStr("─", boxInnerWidth) + "┘";
            std::ostringstream botBoxFormatted;
            botBoxFormatted << color::GRAY << botBoxRaw << color::RESET;
            printCenteredLine(botBoxRaw, botBoxFormatted.str(), innerWidth);
        } else {
            std::ostringstream optFormatted;
            optFormatted << color::CYAN << "[" << opt.key << "]" << color::RESET
                         << color::WHITE << "  " << opt.label << color::RESET;
            printCenteredLine(optTextRaw, optFormatted.str(), innerWidth);
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
    printCenteredLine(footerRaw, footerFormatted.str(), innerWidth);

    // Borde inferior del marco exterior: └────────────────────┘
    std::cout << color::WHITE << "└" << repeatStr("─", innerWidth) << "┘" << color::RESET << "\n";
}

} // namespace conway::ui

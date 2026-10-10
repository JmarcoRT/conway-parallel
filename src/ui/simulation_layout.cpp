#include "ui/simulation_layout.hpp"

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

// Calcula la longitud visual en terminal ignorando secuencias de escape ANSI
// y contando caracteres UTF-8 adecuadamente.
std::size_t visualLength(const std::string& str) {
    std::size_t len = 0;
    bool inEscape = false;
    for (std::size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\033') {
            inEscape = true;
            continue;
        }
        if (inEscape) {
            if (str[i] == 'm' || str[i] == 'K' || str[i] == 'J') {
                inEscape = false;
            }
            continue;
        }
        unsigned char c = static_cast<unsigned char>(str[i]);
        if ((c & 0xC0) != 0x80) {
            len++;
        }
    }
    return len;
}

// Trunca una cadena respetando secuencias ANSI y codepoints UTF-8, agregando "…" si excede maxWidth
std::string truncateToVisualWidth(const std::string& str, std::size_t maxWidth) {
    if (maxWidth == 0) return "";
    if (visualLength(str) <= maxWidth) {
        return str;
    }

    std::size_t targetVisible = (maxWidth > 1) ? (maxWidth - 1) : 0;
    std::string result;
    std::size_t curVisible = 0;
    bool inEscape = false;

    for (std::size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\033') {
            inEscape = true;
            result += str[i];
            continue;
        }
        if (inEscape) {
            result += str[i];
            if (str[i] == 'm' || str[i] == 'K' || str[i] == 'J') {
                inEscape = false;
            }
            continue;
        }

        unsigned char c = static_cast<unsigned char>(str[i]);
        bool isLead = ((c & 0xC0) != 0x80);
        if (isLead && curVisible >= targetVisible) {
            break;
        }
        result += str[i];
        if (isLead) {
            curVisible++;
        }
    }

    if (inEscape) {
        result += color::RESET;
    }
    result += "…";
    return result;
}

// Rellena una cadena (que puede incluir códigos ANSI) a un ancho visual exacto
std::string padToVisualWidth(const std::string& str, std::size_t targetWidth, bool center = false) {
    std::size_t vlen = visualLength(str);
    if (vlen >= targetWidth) {
        return str;
    }
    std::size_t diff = targetWidth - vlen;
    if (center) {
        std::size_t left = diff / 2;
        std::size_t right = diff - left;
        return std::string(left, ' ') + str + std::string(right, ' ');
    } else {
        return str + std::string(diff, ' ');
    }
}

// Formatea números con separador de miles (ej. 302415 -> "302,415")
std::string formatThousands(std::size_t number) {
    std::string s = std::to_string(number);
    int insertPosition = static_cast<int>(s.length()) - 3;
    while (insertPosition > 0) {
        s.insert(static_cast<std::size_t>(insertPosition), ",");
        insertPosition -= 3;
    }
    return s;
}

// Imprime una línea dentro del marco exterior principal
void printFrameLine(
    const std::string& formattedText,
    std::size_t innerWidth,
    std::size_t leftMargin = 0,
    bool center = false
) {
    std::size_t textVisualLen = visualLength(formattedText);
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

void printEmptyLine(std::size_t innerWidth) {
    printFrameLine("", innerWidth, 0, false);
}

} // namespace

void renderSimulationLayout(
    const SimulationHeaderInfo& header,
    const ExecutionInfo& execution,
    const BoardPanelData& currentPanel,
    const BoardPanelData& nextPanel,
    const SimulationLayoutConfig& config
) {
    const std::size_t frameWidth = std::max<std::size_t>(config.width, 76);
    const std::size_t innerWidth = frameWidth - 2;

    // 1. Marco exterior superior
    std::cout << color::WHITE << "┌" << repeatStr("─", innerWidth) << "┐" << color::RESET << "\n";

    // 2. Encabezado principal: Título y Modo
    std::ostringstream titleStream;
    titleStream << color::CYAN << color::BOLD << "──── SIMULACIÓN: JUEGO DE LA VIDA ────" << color::RESET;
    printFrameLine(titleStream.str(), innerWidth, 0, true);

    std::ostringstream modeStream;
    modeStream << color::WHITE << "Modo: " << color::GREEN << color::BOLD << header.mode << color::RESET;
    printFrameLine(modeStream.str(), innerWidth, 0, true);

    printEmptyLine(innerWidth);

    // 3. Línea informativa de archivo cargado y estado
    std::ostringstream infoLeft;
    infoLeft << color::WHITE << "Entrada: " << color::GREEN << header.filename << color::RESET;
    if (header.boardWidth > 0 && header.boardHeight > 0) {
        infoLeft << color::GRAY << " (" << header.boardWidth << " x " << header.boardHeight << ")" << color::RESET;
    }

    std::ostringstream infoRight;
    infoRight << color::WHITE << "Estado: " << color::CYAN << header.statusText << color::RESET;

    std::size_t leftLen = visualLength(infoLeft.str());
    std::size_t rightLen = visualLength(infoRight.str());
    std::size_t spacing = 4;
    if (leftLen + rightLen + 6 < innerWidth) {
        spacing = innerWidth - leftLen - rightLen - 6;
    }

    std::string headerInfoCombined = infoLeft.str() + repeatStr(" ", spacing) + infoRight.str();
    printFrameLine(headerInfoCombined, innerWidth, 3, false);

    // Separador horizontal intermedio
    std::cout << color::WHITE << "├" << repeatStr("─", innerWidth) << "┤" << color::RESET << "\n";

    // 4. Zona de Paneles de Tablero (Panel 1: Current vs Panel 2: Next)
    // Se distribuyen en dos columnas simétricas dentro del marco
    const std::size_t sideMargin = 2;
    const std::size_t betweenMargin = 2;
    const std::size_t availableWidth = (innerWidth > (2 * sideMargin + betweenMargin))
        ? (innerWidth - 2 * sideMargin - betweenMargin)
        : 60;
    const std::size_t panelWidth = availableWidth / 2;
    const std::size_t subInnerWidth = (panelWidth > 2) ? (panelWidth - 2) : 10;

    // Generar borde superior de cada sub-panel con su título
    auto makePanelTop = [subInnerWidth](const std::string& title) -> std::string {
        std::string t = title.empty() ? "Panel" : title;
        std::size_t tLen = visualLength(t);
        std::ostringstream oss;
        oss << color::GRAY << "┌── " << color::RESET << color::CYAN << color::BOLD << t << color::RESET << color::GRAY << " ";
        // Ancho exterior del sub-panel es subInnerWidth + 2
        // Caracteres fijos: "┌── " (4) + t (tLen) + " " (1) + "┐" (1) = tLen + 6
        std::size_t fixedChars = tLen + 6;
        if (subInnerWidth + 2 > fixedChars) {
            oss << repeatStr("─", (subInnerWidth + 2) - fixedChars);
        }
        oss << "┐" << color::RESET;
        return oss.str();
    };

    auto makePanelBottom = [subInnerWidth]() -> std::string {
        std::ostringstream oss;
        oss << color::GRAY << "└" << repeatStr("─", subInnerWidth) << "┘" << color::RESET;
        return oss.str();
    };

    std::string curTop = makePanelTop(currentPanel.title.empty() ? "CURRENT" : currentPanel.title);
    std::string nextTop = makePanelTop(nextPanel.title.empty() ? "NEXT (en construcción)" : nextPanel.title);

    std::string topRowCombined = curTop + repeatStr(" ", betweenMargin) + nextTop;
    printFrameLine(topRowCombined, innerWidth, sideMargin, false);

    // Líneas internas de los dos paneles
    const std::size_t totalLines = std::max({config.innerPanelHeight, currentPanel.lines.size(), nextPanel.lines.size()});
    const std::size_t midLine = totalLines / 2;

    for (std::size_t i = 0; i < totalLines; ++i) {
        std::string curContent;
        if (currentPanel.lines.empty()) {
            if (i == midLine) {
                curContent = std::string(color::GRAY) + "(sin datos)" + color::RESET;
            }
        } else if (i < currentPanel.lines.size()) {
            curContent = truncateToVisualWidth(currentPanel.lines[i], subInnerWidth);
        }

        std::string nextContent;
        if (nextPanel.lines.empty()) {
            if (i == midLine) {
                nextContent = std::string(color::GRAY) + "(sin datos)" + color::RESET;
            }
        } else if (i < nextPanel.lines.size()) {
            nextContent = truncateToVisualWidth(nextPanel.lines[i], subInnerWidth);
        }

        std::string curPadded = padToVisualWidth(curContent, subInnerWidth, true);
        std::string nextPadded = padToVisualWidth(nextContent, subInnerWidth, true);

        std::ostringstream rowStream;
        rowStream << color::GRAY << "│" << color::RESET
                  << curPadded
                  << color::GRAY << "│" << color::RESET
                  << repeatStr(" ", betweenMargin)
                  << color::GRAY << "│" << color::RESET
                  << nextPadded
                  << color::GRAY << "│" << color::RESET;

        printFrameLine(rowStream.str(), innerWidth, sideMargin, false);
    }

    // Borde inferior de los dos paneles
    std::string bottomRowCombined = makePanelBottom() + repeatStr(" ", betweenMargin) + makePanelBottom();
    printFrameLine(bottomRowCombined, innerWidth, sideMargin, false);

    // Separador horizontal antes del panel de información de ejecución
    std::cout << color::WHITE << "├" << repeatStr("─", innerWidth) << "┤" << color::RESET << "\n";

    // 5. Panel 3: Zona de Información de Ejecución
    std::ostringstream execTitle;
    execTitle << color::WHITE << color::BOLD << "── INFORMACIÓN DE EJECUCIÓN ──" << color::RESET;
    printFrameLine(execTitle.str(), innerWidth, 3, false);

    // Métricas principales (Generación, Vivas, Progreso)
    std::ostringstream metricsStream;
    metricsStream << color::WHITE << "Generación: " << color::GREEN << execution.generation << color::RESET
                  << "    "
                  << color::WHITE << "Células vivas: " << color::GREEN << formatThousands(execution.aliveCount) << color::RESET
                  << "    "
                  << color::WHITE << "Progreso paso: " << color::CYAN << execution.stepProgress << color::RESET;
    printFrameLine(metricsStream.str(), innerWidth, 3, false);

    // Zona reservada para workers/hilos/halos futuros (solo si está definida)
    if (!execution.workersInfo.empty()) {
        std::ostringstream workerStream;
        workerStream << color::WHITE << "Workers: " << color::GRAY << execution.workersInfo << color::RESET;
        printFrameLine(workerStream.str(), innerWidth, 3, false);
    }

    // Separador horizontal inferior
    std::cout << color::WHITE << "├" << repeatStr("─", innerWidth) << "┤" << color::RESET << "\n";

    // 6. Footer de navegación / controles interactivos (estilo S5)
    std::ostringstream footerStream;
    footerStream << color::GRAY << "[Espacio] Pausa/Reanudar   [N] Paso   [R] Reiniciar   [Esc] Menú" << color::RESET;
    printFrameLine(footerStream.str(), innerWidth, 0, true);

    // Marco exterior inferior
    std::cout << color::WHITE << "└" << repeatStr("─", innerWidth) << "┘" << color::RESET << "\n";
}

} // namespace conway::ui

#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace conway::ui {

// Constantes ANSI para formato y colores en terminal
namespace color {
    constexpr const char* RESET     = "\033[0m";
    constexpr const char* BOLD      = "\033[1m";
    constexpr const char* DIM       = "\033[2m";
    constexpr const char* CYAN      = "\033[1;36m";
    constexpr const char* GREEN     = "\033[1;32m";
    constexpr const char* WHITE     = "\033[1;37m";
    constexpr const char* GRAY      = "\033[90m";
}

// Representación de información de la entrada activa para la UI
struct ActiveInputInfo {
    bool hasInput{false};
    std::string filename{"ninguna"};
    std::size_t width{0};
    std::size_t height{0};
    std::size_t aliveCells{0};
};

// Ítem individual para menús de selección
struct MenuItem {
    std::string key;    // Ej: "1", "2", "I", "C", "0"
    std::string label;  // Ej: "Benchmark", "Visualización", "Salir"
};

// Configuración general del layout base reutilizable
struct LayoutConfig {
    std::size_t width{78};
    std::size_t height{24};
    bool colorEnabled{true};
};

/**
 * Renderiza una pantalla completa utilizando el layout base reutilizable.
 * Función pura de renderizado sin captura de entrada ni loops de control.
 */
void renderLayout(
    const std::string& title,
    const std::string& subtitle,
    const ActiveInputInfo& inputInfo,
    const std::vector<MenuItem>& options,
    std::size_t selectedIndex,
    const LayoutConfig& config = LayoutConfig{},
    const std::string& notice = {}
);

void renderScreen(
    const std::string& title,
    const std::string& subtitle,
    const std::vector<std::string>& body,
    const std::string& footer,
    const LayoutConfig& config
);

} // namespace conway::ui

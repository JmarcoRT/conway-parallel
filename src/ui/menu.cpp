#include "ui/menu.hpp"

namespace conway::ui {

void renderMainMenu(
    std::size_t selectedIndex,
    const ActiveInputInfo& inputInfo
) {
    static const std::vector<MenuItem> mainOptions = {
        {"1", "Benchmark"},
        {"2", "Visualización"},
        {"0", "Salir"}
    };

    renderLayout(
        "CONWAY PARALLEL",
        "Juego de la Vida",
        inputInfo,
        mainOptions,
        selectedIndex
    );
}

} // namespace conway::ui

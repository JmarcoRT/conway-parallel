#include "ui/menu.hpp"

namespace conway::ui {

void renderMainMenu(
    std::size_t selectedIndex,
    const ActiveInputInfo& inputInfo
) {
    static const std::vector<MenuItem> mainOptions = {
        {"D", "Visualización didáctica"}, {"B", "Benchmark"},
        {"C", "Catálogo de patrones"}, {"M", "Seleccionar motor"},
        {"R", "Configurar recursos"}, {"Q", "Salir"}
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

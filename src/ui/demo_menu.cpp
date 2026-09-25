// DEMO S5 - Demostración aislada de UI (Layout Base y Menú Principal v1)
// Reemplazar cuando Manuel integre el loop interactivo de navegación.

#include "ui/menu.hpp"
#include <iostream>

int main() {
    std::cout << "=== DEMO S5: MENU PRINCIPAL V1 & BASE LAYOUT ===\n\n";

    conway::ui::ActiveInputInfo infoWithInput;
    infoWithInput.hasInput = true;
    infoWithInput.filename = "tablero.txt";
    infoWithInput.width = 1000;
    infoWithInput.height = 1000;
    infoWithInput.aliveCells = 302415;

    // CASO 1: Estado sin entrada activa, opción [1] Benchmark seleccionada (index 0)
    std::cout << "--- ESTADO 1: Sin entrada activa (selectedIndex = 0: Benchmark) ---\n\n";
    conway::ui::ActiveInputInfo infoNoInput;
    infoNoInput.hasInput = false;
    conway::ui::renderMainMenu(0, infoNoInput);

    std::cout << "\n\n";

    // CASO 2: Estado con entrada activa, opción [2] Visualización seleccionada (index 1)
    std::cout << "--- ESTADO 2: Con entrada activa (selectedIndex = 1: Visualización) ---\n\n";
    conway::ui::renderMainMenu(1, infoWithInput);

    std::cout << "\n\n";

    // CASO 3: Opción [0] Salir seleccionada (selectedIndex = 2)
    std::cout << "--- ESTADO 3: Opción [0] Salir resaltada (selectedIndex = 2) ---\n\n";
    conway::ui::renderMainMenu(2, infoWithInput);

    std::cout << "\n\n";

    // CASO 4: Índice fuera de rango (selectedIndex = 99 - sin resaltar ninguna opción)
    std::cout << "--- ESTADO 4: Índice fuera de rango (selectedIndex = 99) ---\n\n";
    conway::ui::renderMainMenu(99, infoWithInput);

    std::cout << "\n";
    return 0;
}

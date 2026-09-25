// DEMO S5 - Demostración aislada de UI (Layout Base y Menú Principal v1)
// Reemplazar cuando Manuel integre el loop interactivo de navegación.

#include "ui/menu.hpp"
#include <iostream>

int main() {
    std::cout << "=== DEMO S5: MENU PRINCIPAL V1 & BASE LAYOUT ===\n\n";

    // CASO 1: Estado sin entrada activa (hasInput = false), opción [1] Benchmark seleccionada (index 0)
    std::cout << "--- ESTADO 1: Sin entrada activa ---\n\n";
    conway::ui::ActiveInputInfo infoNoInput;
    infoNoInput.hasInput = false;

    conway::ui::renderMainMenu(0, infoNoInput);

    std::cout << "\n\n";

    // CASO 2: Estado con entrada activa cargada (hasInput = true), opción [2] Visualización seleccionada (index 1)
    std::cout << "--- ESTADO 2: Con entrada activa cargada ---\n\n";
    conway::ui::ActiveInputInfo infoWithInput;
    infoWithInput.hasInput = true;
    infoWithInput.filename = "tablero.txt";
    infoWithInput.width = 1000;
    infoWithInput.height = 1000;
    infoWithInput.aliveCells = 302415;

    conway::ui::renderMainMenu(1, infoWithInput);

    std::cout << "\n";
    return 0;
}

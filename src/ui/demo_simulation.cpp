// DEMO S6 - Demostración aislada del Layout de Simulación (3 paneles)
// Muestra la distribución visual para los 4 modos futuros sin bloquear
// el target principal ni depender del contenido de simulación de Judith.

#include "ui/simulation_layout.hpp"
#include <iostream>
#include <vector>

int main() {
    std::cout << "=== DEMO S6: LAYOUT DE SIMULACION (3 PANELES) ===\n\n";

    // Mock puramente estático/fijo para presentación visual (sin lógica de Conway en UI)
    // Muestra una evolución representativa reconocible (Blinker horizontal -> vertical, Block estable)
    conway::ui::BoardPanelData currentMock;
    currentMock.title = "CURRENT";
    currentMock.lines = {
        ". . . . . . . . . .",
        ". . . . . . . . . .",
        ". . . # # # . . . .",
        ". . . . . . . . . .",
        ". . . . . . . . . .",
        ". . . . . . # # . .",
        ". . . . . . # # . .",
        ". . . . . . . . . ."
    };

    conway::ui::BoardPanelData nextMock;
    nextMock.title = "NEXT (en construcción)";
    nextMock.lines = {
        ". . . . . . . . . .",
        ". . . . # . . . . .",
        ". . . . # . . . . .",
        ". . . . # . . . . .",
        ". . . . . . . . . .",
        ". . . . . . # # . .",
        ". . . . . . # # . .",
        ". . . . . . . . . ."
    };

    conway::ui::ExecutionInfo execInfo;
    execInfo.generation = 42;
    execInfo.aliveCount = 302415;
    execInfo.stepProgress = "100%";
    execInfo.workersInfo = "—";

    conway::ui::SimulationLayoutConfig config;
    config.width = 84;
    config.innerPanelHeight = 8;

    // Los 4 modos futuros con idéntica distribución y placeholders genéricos:
    const std::vector<std::string> modes = {
        "Secuencial",
        "OpenMP",
        "Pthreads",
        "MPI"
    };

    for (std::size_t i = 0; i < modes.size(); ++i) {
        std::cout << "------------------------------------------------------------\n";
        std::cout << " CASO " << (i + 1) << ": Layout para Pantalla Modo " << modes[i] << "\n";
        std::cout << "------------------------------------------------------------\n\n";

        conway::ui::SimulationHeaderInfo header;
        header.mode = modes[i];
        header.filename = "blinker.txt";
        header.boardWidth = 30;
        header.boardHeight = 30;
        header.statusText = "Pausado";

        conway::ui::renderSimulationLayout(header, execInfo, currentMock, nextMock, config);
        std::cout << "\n\n";
    }

    // CASO 5: Caso de robustez - CURRENT vacío muestra "(sin datos)" y NEXT demuestra truncamiento "…"
    std::cout << "------------------------------------------------------------\n";
    std::cout << " CASO 5: Robustez - CURRENT vacío muestra (sin datos) y NEXT con truncamiento\n";
    std::cout << "------------------------------------------------------------\n\n";

    conway::ui::BoardPanelData emptyCurrent;
    emptyCurrent.title = "CURRENT";
    emptyCurrent.lines = {}; // Sin líneas -> genera automáticamente '(sin datos)' centrado

    conway::ui::BoardPanelData longLineNext;
    longLineNext.title = "NEXT (en construcción)";
    longLineNext.lines = {
        ". . . . . . . . . .",
        "Linea super larga que excede el ancho maximo del panel y debe ser truncada con puntos suspensivos",
        ". . . . # . . . . .",
        ". . . . # . . . . .",
        ". . . . # . . . . .",
        ". . . . . . # # . .",
        ". . . . . . # # . .",
        ". . . . . . . . . ."
    };

    conway::ui::SimulationHeaderInfo headerCase5;
    headerCase5.mode = "Secuencial";
    headerCase5.filename = "tablero.txt";
    headerCase5.boardWidth = 1000;
    headerCase5.boardHeight = 1000;
    headerCase5.statusText = "Inicializando";

    conway::ui::renderSimulationLayout(headerCase5, execInfo, emptyCurrent, longLineNext, config);
    std::cout << "\n\n";

    std::cout << "=== FIN DE DEMO S6 ===\n";
    return 0;
}

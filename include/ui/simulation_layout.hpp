#pragma once

#include "ui/layout.hpp"

#include <string>
#include <ostream>
#include <vector>
#include <cstddef>

namespace conway::ui {

// Metadatos de la cabecera de la simulación
struct SimulationHeaderInfo {
    std::string mode{"Secuencial"};       // Ej: "Secuencial", "OpenMP", "Pthreads", "MPI"
    std::string filename{"ninguna"};      // Archivo de patrón cargado
    std::size_t boardWidth{0};            // Ancho lógico del tablero
    std::size_t boardHeight{0};           // Alto lógico del tablero
    std::string statusText{"Pausado"};    // Ej: "En ejecución", "Pausado", "Completado"
};

// Información de ejecución y métricas
struct ExecutionInfo {
    std::size_t generation{0};            // Generación actual
    std::size_t aliveCount{0};            // Conteo de células vivas
    std::string stepProgress{"100%"};     // Progreso de cálculo del paso actual
    std::string workersInfo{"—"};         // Placeholder para workers/threads/MPI (por defecto "—")
};

// Contenido de un panel de tablero (Current o Next)
// Permite desacoplar el layout del renderizado de celdas
struct BoardPanelData {
    std::string title{"CURRENT"};         // Título del panel (ej. "CURRENT", "NEXT (en construcción)")
    std::vector<std::string> lines;       // Líneas internas del panel (representación de celdas)
};

// Configuración visual del layout de simulación
struct SimulationLayoutConfig {
    std::size_t width{82};                // Ancho total del marco exterior en caracteres
    std::size_t innerPanelHeight{8};      // Alto de líneas para el interior de cada panel de tablero
    bool colorEnabled{true};
};

/**
 * Renderiza de forma pura la pantalla de simulación con la distribución de los 3 paneles:
 * 1. Panel Current (t)
 * 2. Panel Next (t+1)
 * 3. Panel de Información de Ejecución
 *
 * Mantiene la consistencia visual y paleta ANSI definida en la Semana 5.
 */
void renderSimulationLayout(
    const SimulationHeaderInfo& header,
    const ExecutionInfo& execution,
    const BoardPanelData& currentPanel,
    const BoardPanelData& nextPanel,
    const SimulationLayoutConfig& config,
    std::ostream& out
);

void renderSimulationLayout(
    const SimulationHeaderInfo& header,
    const ExecutionInfo& execution,
    const BoardPanelData& currentPanel,
    const BoardPanelData& nextPanel,
    const SimulationLayoutConfig& config = SimulationLayoutConfig{}
);

} // namespace conway::ui

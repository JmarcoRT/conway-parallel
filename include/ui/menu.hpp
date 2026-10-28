#pragma once

#include "ui/layout.hpp"
#include <cstddef>

namespace conway::ui {

/**
 * Renderiza la vista del Menú Principal (v1).
 * Función pura de renderizado que pasa las opciones principales al layout base.
 *
 * @param selectedIndex Índice de acción: Didáctico, Benchmark, Catálogo, Motor, Recursos, Salir.
 * @param inputInfo Datos de la entrada activa.
 */
void renderMainMenu(
    std::size_t selectedIndex = 0,
    const ActiveInputInfo& inputInfo = ActiveInputInfo{}
);

} // namespace conway::ui

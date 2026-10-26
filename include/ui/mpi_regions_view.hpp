#pragma once

#include "mpi/region_metadata.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace conway::ui {

// Presentacion textual de la descomposicion MPI (regiones por rank y halos).
//
// Consume exclusivamente mpi::DecompositionLayout / DecompositionSnapshot
// (describe_decomposition o read_decomposition_csv). No llama a MPI, no
// calcula particiones propias ni contiene reglas de Conway; solo da formato.
// Las lineas son ASCII, para poder envolverlas en elementos FTXUI (text()) o
// imprimirlas en consola sin depender del ancho de glifos Unicode.

struct MpiRegionsRenderOptions {
    // Si el alto supera este valor, el mapa de filas muestra solo las filas
    // frontera de cada region y resume las interiores en una linea "...".
    std::size_t maxRowMapRows{32};
    // Incluir columnas de metricas en la tabla cuando la instantanea las tiene.
    bool showMetrics{true};
};

// Papel de una fila global respecto al intercambio de halos de una generacion.
//   owner      rank que la calcula.
//   haloOfUp   rank cuyo halo INFERIOR recibe esta fila (kNoNeighbor si ninguno).
//   haloOfDown rank cuyo halo SUPERIOR recibe esta fila (kNoNeighbor si ninguno).
// Sirve para colorear el tablero por rank y marcar filas frontera en la UI.
struct MpiRowInfo {
    int owner{mpi::kNoNeighbor};
    int haloOfUp{mpi::kNoNeighbor};
    int haloOfDown{mpi::kNoNeighbor};

    bool isBoundary() const noexcept {
        return haloOfUp != mpi::kNoNeighbor || haloOfDown != mpi::kNoNeighbor;
    }
};

// Un elemento por fila global (tamano = layout.height).
std::vector<MpiRowInfo> mpiRowInfo(const mpi::DecompositionLayout& layout);

// Cabecera + tabla por rank: filas propias, origen de cada halo y, si hay,
// metricas acumuladas (vivas, segundos de halo y de computo, bytes enviados).
std::vector<std::string> mpiRegionTableLines(const mpi::DecompositionSnapshot& snapshot,
                                             const MpiRegionsRenderOptions& options = {});

// Mapa fila -> rank, indicando a que halo viaja cada fila frontera.
std::vector<std::string> mpiRowMapLines(const mpi::DecompositionLayout& layout,
                                        const MpiRegionsRenderOptions& options = {});

// Resumen de una linea para ExecutionInfo::workersInfo (simulation_layout.hpp),
// p. ej. "MPI: 4 procesos (4 con filas) | halos 6 msg/gen, 42 B/gen".
std::string mpiWorkersSummary(const mpi::DecompositionLayout& layout);

} // namespace conway::ui

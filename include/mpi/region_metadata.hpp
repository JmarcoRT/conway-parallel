#pragma once

#include "mpi/halo.hpp"

#include <cstddef>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace conway::mpi {

// Metadatos de regiones y halos de la descomposicion MPI, pensados para la UI.
//
// Esta cabecera NO incluye <mpi.h>: la UI (proceso FTXUI, sin mpirun) puede
// construir la descripcion con describe_decomposition() o leer la que escribe
// conway_mpi (--metadata) con read_decomposition_csv(). La geometria sale de
// partition_rows/halo_neighbors; no hay una segunda formula de reparto.
// Todas las filas son indices GLOBALES base 0 del tablero logico (sin halo).

// Un halo (fila local 0 o height+1) de la region de un rank.
struct HaloEdge {
    int sourceRank{kNoNeighbor};      // rank que envia la fila; kNoNeighbor = borde global
    std::size_t sourceGlobalRow{0};   // fila global que se copia (solo si sourceRank valido)

    bool isGlobalBorder() const noexcept { return sourceRank == kNoNeighbor; }
};

struct RankRegion {
    int rank{0};
    std::size_t firstRow{0};          // primera fila propia
    std::size_t rowCount{0};          // filas propias (0 = rank sin trabajo)
    HaloEdge top;                     // llena la fila local 0
    HaloEdge bottom;                  // llena la fila local rowCount + 1
    std::size_t localBufferCells{0};  // celdas de UN buffer local: (width+2)*(rowCount+2)

    std::size_t endRow() const noexcept { return firstRow + rowCount; }  // exclusivo
    bool empty() const noexcept { return rowCount == 0; }
};

struct DecompositionLayout {
    std::size_t width{0};
    std::size_t height{0};
    int processes{1};
    std::vector<RankRegion> regions;          // uno por rank, en orden de rank
    std::size_t haloMessagesPerStep{0};       // mensajes de halo por generacion (todos los ranks)
    std::size_t haloBytesPerStep{0};          // bytes de celulas por generacion (todos los ranks)
};

// Lanza std::invalid_argument si width/height son 0 o processes < 1.
DecompositionLayout describe_decomposition(std::size_t width, std::size_t height,
                                           int processes);

// Rank que posee la fila global `row`; kNoNeighbor si esta fuera del tablero.
int owner_of_row(const DecompositionLayout& layout, std::size_t row);

// Observaciones de una ejecucion, por rank. Son acumuladas sobre el lote de
// generaciones (no por generacion) y provienen de core::MpiStepStats.
struct RankRunMetrics {
    int rank{0};
    std::size_t aliveCount{0};        // vivas locales al terminar
    double haloSeconds{0.0};
    double computeSeconds{0.0};
    std::size_t messagesSent{0};
    std::size_t bytesSent{0};
};

struct DecompositionSnapshot {
    DecompositionLayout layout;
    std::size_t generation{0};
    std::string haloMode;                 // "blocking", "overlap" o vacio si no se ejecuto
    std::vector<RankRunMetrics> metrics;  // vacio = solo geometria; si no, uno por rank
};

// Contrato CSV (version 1). Lineas:
//   # conway-mpi-decomposition v1
//   width,height,processes,generation,halo_mode
//   <valores>
//   rank,first_row,row_count,halo_top_rank,halo_top_row,halo_bottom_rank,
//     halo_bottom_row,local_buffer_cells,alive,halo_seconds,compute_seconds,
//     messages_sent,bytes_sent
//   <una fila por rank>
// halo_*_rank = -1 y halo_*_row vacio indican borde global. Las columnas de
// metricas quedan vacias si la instantanea no tiene ejecucion. Los segundos
// se escriben con la representacion mas corta que se relee exacta.
void write_decomposition_csv(std::ostream& out, const DecompositionSnapshot& snap);

// Valida cabeceras, numero de filas y que la geometria coincida exactamente
// con describe_decomposition(width, height, processes): la UI no recibe una
// region u halo que el motor no use. Lanza std::runtime_error si no.
DecompositionSnapshot read_decomposition_csv(std::istream& in);

} // namespace conway::mpi

#pragma once

#include "app/mpi_didactic_view.hpp"
#include "mpi/window_trace.hpp"

namespace conway::app::didactic_view {

inline constexpr std::array clusterStages{DidacticStage::MPIState, DidacticStage::MPIExchangeFirst,
    DidacticStage::MPIExchangeSecond, DidacticStage::MPICell, DidacticStage::MPIBuffers,
    DidacticStage::GenerationResult};

inline std::string rowRange(std::size_t first, std::size_t end) {
    return "[" + std::to_string(first) + ", " + std::to_string(end) + ")";
}

inline void clusterPartition(Blocks& out, const mpi::RowPartition& part) {
    text(out, "R" + std::to_string(part.rank) + " · filas " + rowRange(part.firstRow, part.endRow()) +
              (part.empty() ? " · sin filas propias" : ""));
}

inline Grid clusterGrid(const mpi::WindowTrace& trace, const std::string& cells,
                        std::size_t first, std::size_t end, int processes) {
    auto board = blank(trace.columns, end - first);
    // At most the ranks intersecting this bounded window have cells to display.
    for (int rank = 0; rank < processes; ++rank) {
        const auto part = mpi::partition_rows(trace.height, processes, rank);
        for (auto row = std::max(first, part.firstRow); row < std::min(end, part.endRow()); ++row) {
            board.labels.push_back("R" + std::to_string(rank) + " g" + std::to_string(row));
            for (std::size_t column = 0; column < trace.columns; ++column) {
                const auto target = (row - first) * trace.columns + column;
                board.values[target] = cells[(row - trace.selection.row) * trace.columns + column] - '0';
                board.known[target] = 1;
                board.owners[target] = rank;
            }
        }
    }
    return board;
}

inline void clusterGlobal(Blocks& out, const mpi::WindowTrace& trace,
                          DidacticStage stage, int processes, bool sameWindow) {
    const bool result = stage == DidacticStage::GenerationResult;
    text(out, "Tablero " + std::to_string(trace.width) + " × " + std::to_string(trace.height) +
              " · Células vivas: " + std::to_string(result ? trace.aliveAfter : trace.aliveBefore));
    if (sameWindow) {
        const auto first = trace.selection.row;
        grid(out, "Ventana global · G" + std::to_string(trace.selection.generation + result),
             clusterGrid(trace, result ? trace.after : trace.before, first, first + trace.rows, processes));
    } else {
        text(out, "Ventana sin captura. Se capturará en el próximo paso.");
    }
    heading(out, "Reparto de filas");
    for (int rank = 0; rank < processes; ++rank)
        clusterPartition(out, mpi::partition_rows(trace.height, processes, rank));
}

inline void clusterLocal(Blocks& out, const mpi::WindowTrace& trace, DidacticStage stage,
                         const mpi::RowPartition& part, bool sameWindow, bool explanation) {
    if (part.empty()) return;
    if (!sameWindow || part.rank != trace.selection.rank) {
        text(out, "Rank o ventana sin captura. Se capturará en el próximo paso.");
        return;
    }
    const auto first = std::max(trace.selection.row, part.firstRow);
    const auto end = std::min(trace.selection.row + trace.rows, part.endRow());
    if (first >= end) {
        text(out, "La ventana queda fuera de las filas de este rank.");
        return;
    }
    const bool result = stage == DidacticStage::GenerationResult;
    const bool progressed = stage == DidacticStage::MPIBuffers;
    const auto generation = trace.selection.generation;
    if (explanation) {
        text(out, "Reproducción de una traza ya capturada. Los ranks evolucionaron el tablero completo.");
        text(out, "Los halos se copiaron de los búferes recibidos durante MPI_Sendrecv.");
    }
    if (stage == DidacticStage::MPIExchangeFirst || stage == DidacticStage::MPIExchangeSecond) {
        const auto exchange = stage == DidacticStage::MPIExchangeFirst ? 0 : 1;
        const auto peer = exchange == 0 ? part.rank + 1 : part.rank - 1;
        const bool fixed = exchange == 0 ? part.endRow() == part.globalHeight : part.firstRow == 0;
        const auto length = trace.halos[exchange].size();
        if (!length) text(out, "El halo queda fuera de esta ventana.");
        else if (fixed) text(out, "Borde global fijo en cero.");
        else text(out, "Recibe de R" + std::to_string(peer) + " · " + std::to_string(length) +
                      " columnas en la ventana.");
    }

    auto own = clusterGrid(trace, result ? trace.after : trace.before, first, end, part.size);
    const bool top = !trace.halos[1].empty(), bottom = !trace.halos[0].empty();
    auto local = blank(trace.columns, own.height + top + bottom, part.rank);
    const auto putHalo = [&](std::size_t target, std::size_t exchange, int peer) {
        const bool fixed = peer < 0 || peer >= part.size;
        local.rowKinds[target] = fixed ? 2 : 1;
        const bool known = !result && exchangesVisible(stage) > static_cast<int>(exchange);
        if (known) {
            for (std::size_t c = 0; c < trace.columns; ++c) {
                const auto i = target * trace.columns + c;
                local.known[i] = 1;
                local.values[i] = trace.halos[exchange][c] - '0';
                local.owners[i] = fixed ? -1 : peer;
            }
        }
        const auto direction = exchange == 1 ? "H↑" : "H↓";
        const auto row = exchange == 1 ? part.firstRow - 1 : part.endRow();
        local.labels.push_back(!known ? std::string(direction) + " no vigente" :
            fixed ? "H borde fijo 0" : "H de R" + std::to_string(peer) + " g" + std::to_string(row));
    };

    if (top) putHalo(0, 1, part.firstRow ? part.rank - 1 : -1);
    local.labels.insert(local.labels.end(), own.labels.begin(), own.labels.end());
    const auto offset = top * trace.columns;
    std::copy(own.values.begin(), own.values.end(), local.values.begin() + offset);
    std::copy(own.known.begin(), own.known.end(), local.known.begin() + offset);
    if (bottom) putHalo(local.height - 1, 0, part.endRow() < part.globalHeight ? part.rank + 1 : -1);

    if (stage == DidacticStage::MPICell && trace.probe) {
        const auto column = trace.columns / 2;
        highlight(local, own.height - 1 + top, column);
        const auto& probe = *trace.probe;
        text(out, "Celda g" + std::to_string(end - 1) + ", c" +
                  std::to_string(trace.selection.column + column) + " · Vecinos vivos: " +
                  std::to_string(probe.neighborCount) + " · " +
                  std::to_string(probe.before) + " → " + std::to_string(probe.after));
        if (explanation) {
            std::string neighbors;
            for (auto value : probe.neighbors) neighbors += static_cast<char>('0' + value);
            text(out, "NW,N,NE,W,E,SW,S,SE: " + neighbors);
        }
    }
    grid(out, (result ? "Tablero actualizado G" : "Generación actual G") +
              std::to_string(generation + result), std::move(local));
    if (progressed || stage == DidacticStage::MPICell) {
        auto next = clusterGrid(trace, trace.after, first, end, part.size);
        if (!progressed) {
            std::fill(next.known.begin(), next.known.end(), 0);
            if (trace.probe) next.known[(next.height - 1) * trace.columns + trace.columns / 2] = 1;
        }
        grid(out, progressed ? "Nueva generación G" + std::to_string(generation + 1) :
                 "Resultado de la celda seleccionada", std::move(next));
    }
}
}

#include "ui/mpi_regions_view.hpp"

#include <cstdio>
#include <string>

namespace conway::ui {

namespace {

std::string pad(std::string text, std::size_t width) {
    if (text.size() < width) {
        text.append(width - text.size(), ' ');
    }
    return text;
}

std::string padLeft(const std::string& text, std::size_t width) {
    return text.size() < width ? std::string(width - text.size(), ' ') + text : text;
}

std::string rankName(int rank) {
    return "r" + std::to_string(rank);
}

std::string range(const mpi::RankRegion& reg) {
    if (reg.empty()) {
        return "(vacia)";
    }
    return "[" + std::to_string(reg.firstRow) + ".." + std::to_string(reg.endRow() - 1) + "]";
}

std::string edge(const mpi::RankRegion& reg, const mpi::HaloEdge& e) {
    if (reg.empty()) {
        return "-";
    }
    if (e.isGlobalBorder()) {
        return "borde (0)";
    }
    return "fila " + std::to_string(e.sourceGlobalRow) + " de " + rankName(e.sourceRank);
}

std::string seconds(double value) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6f", value);
    return buf;
}

std::string rowLine(std::size_t row, const MpiRowInfo& info) {
    std::string line = padLeft(std::to_string(row), 5) + "  " + pad(rankName(info.owner), 5);
    if (info.haloOfUp != mpi::kNoNeighbor && info.haloOfDown != mpi::kNoNeighbor) {
        line += "-> halo inf. de " + rankName(info.haloOfUp) + " y halo sup. de " +
                rankName(info.haloOfDown);
    } else if (info.haloOfUp != mpi::kNoNeighbor) {
        line += "-> halo inf. de " + rankName(info.haloOfUp);
    } else if (info.haloOfDown != mpi::kNoNeighbor) {
        line += "-> halo sup. de " + rankName(info.haloOfDown);
    }
    while (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
    return line;
}

} // namespace

std::vector<MpiRowInfo> mpiRowInfo(const mpi::DecompositionLayout& layout) {
    std::vector<MpiRowInfo> rows(layout.height);
    for (const auto& reg : layout.regions) {
        for (std::size_t r = reg.firstRow; r < reg.endRow(); ++r) {
            rows[r].owner = reg.rank;
        }
        // El halo superior de reg recibe la ultima fila del rank de arriba,
        // que por tanto viaja "hacia abajo" (haloOfDown de esa fila).
        if (!reg.empty() && !reg.top.isGlobalBorder()) {
            rows[reg.top.sourceGlobalRow].haloOfDown = reg.rank;
        }
        if (!reg.empty() && !reg.bottom.isGlobalBorder()) {
            rows[reg.bottom.sourceGlobalRow].haloOfUp = reg.rank;
        }
    }
    return rows;
}

std::vector<std::string> mpiRegionTableLines(const mpi::DecompositionSnapshot& snapshot,
                                             const MpiRegionsRenderOptions& options) {
    const auto& L = snapshot.layout;
    std::vector<std::string> lines;
    std::string head = "MPI: " + std::to_string(L.processes) + " procesos | tablero " +
                       std::to_string(L.height) + "x" + std::to_string(L.width) +
                       " | generacion " + std::to_string(snapshot.generation);
    if (!snapshot.haloMode.empty()) {
        head += " | halos: " + snapshot.haloMode;
    }
    lines.push_back(head);
    lines.push_back("Halos por generacion: " + std::to_string(L.haloMessagesPerStep) +
                    " mensajes, " + std::to_string(L.haloBytesPerStep) + " bytes");

    const bool metrics = options.showMetrics && snapshot.metrics.size() == L.regions.size();
    std::string header = pad("rank", 6) + pad("filas", 13) + padLeft("n", 6) + "  " +
                         pad("halo sup.", 18) + pad("halo inf.", 18);
    if (metrics) {
        header += padLeft("vivas", 8) + padLeft("halo(s)", 12) + padLeft("computo(s)", 12) +
                  padLeft("bytes env.", 12);
    }
    lines.push_back(header);
    for (std::size_t i = 0; i < L.regions.size(); ++i) {
        const auto& reg = L.regions[i];
        std::string line = pad(rankName(reg.rank), 6) + pad(range(reg), 13) +
                           padLeft(std::to_string(reg.rowCount), 6) + "  " +
                           pad(edge(reg, reg.top), 18) + pad(edge(reg, reg.bottom), 18);
        if (metrics) {
            const auto& m = snapshot.metrics[i];
            line += padLeft(std::to_string(m.aliveCount), 8) + padLeft(seconds(m.haloSeconds), 12) +
                    padLeft(seconds(m.computeSeconds), 12) + padLeft(std::to_string(m.bytesSent), 12);
        }
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> mpiRowMapLines(const mpi::DecompositionLayout& layout,
                                        const MpiRegionsRenderOptions& options) {
    const auto rows = mpiRowInfo(layout);
    std::vector<std::string> lines;
    lines.push_back(" fila  rank frontera");
    const bool full = layout.height <= options.maxRowMapRows;
    for (const auto& reg : layout.regions) {
        if (reg.empty()) {
            continue;
        }
        if (full || reg.rowCount <= 2) {
            for (std::size_t r = reg.firstRow; r < reg.endRow(); ++r) {
                lines.push_back(rowLine(r, rows[r]));
            }
            continue;
        }
        lines.push_back(rowLine(reg.firstRow, rows[reg.firstRow]));
        lines.push_back("  ...  " + pad(rankName(reg.rank), 5) + "(" +
                        std::to_string(reg.rowCount - 2) + " filas interiores)");
        lines.push_back(rowLine(reg.endRow() - 1, rows[reg.endRow() - 1]));
    }
    std::size_t empty = 0;
    for (const auto& reg : layout.regions) {
        empty += reg.empty() ? 1u : 0u;
    }
    if (empty > 0) {
        lines.push_back("Ranks sin filas: " + std::to_string(empty) +
                        " (no calculan ni intercambian halos)");
    }
    return lines;
}

std::string mpiWorkersSummary(const mpi::DecompositionLayout& layout) {
    std::size_t active = 0;
    for (const auto& reg : layout.regions) {
        active += reg.empty() ? 0u : 1u;
    }
    return "MPI: " + std::to_string(layout.processes) + " procesos (" + std::to_string(active) +
           " con filas) | halos " + std::to_string(layout.haloMessagesPerStep) + " msg/gen, " +
           std::to_string(layout.haloBytesPerStep) + " B/gen";
}

} // namespace conway::ui

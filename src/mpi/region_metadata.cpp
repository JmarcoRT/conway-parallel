#include "mpi/region_metadata.hpp"

#include "mpi/partition.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace conway::mpi {

namespace {

constexpr std::string_view kMagic = "# conway-mpi-decomposition v1";
constexpr std::string_view kGlobalHeader = "width,height,processes,generation,halo_mode";
constexpr std::string_view kRankHeader =
    "rank,first_row,row_count,halo_top_rank,halo_top_row,halo_bottom_rank,"
    "halo_bottom_row,local_buffer_cells,alive,halo_seconds,compute_seconds,"
    "messages_sent,bytes_sent";

std::string number(double v) {
    std::array<char, 64> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    if (res.ec != std::errc{}) {
        throw std::runtime_error("no se pudo formatear un numero");
    }
    return std::string(buf.data(), res.ptr);
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out(1);
    for (char c : line) {
        if (c == ',') {
            out.emplace_back();
        } else if (c != '\r') {
            out.back().push_back(c);
        }
    }
    return out;
}

template <class T>
T parse(const std::string& text, const char* what) {
    T value{};
    const auto res = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || res.ec != std::errc{} || res.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::string("CSV de descomposicion: valor invalido en ") + what);
    }
    return value;
}

std::string next_line(std::istream& in, const char* what) {
    std::string line;
    if (!std::getline(in, line)) {
        throw std::runtime_error(std::string("CSV de descomposicion: falta ") + what);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

bool same_edge(const HaloEdge& a, const HaloEdge& b) {
    return a.sourceRank == b.sourceRank &&
           (a.isGlobalBorder() || a.sourceGlobalRow == b.sourceGlobalRow);
}

} // namespace

DecompositionLayout describe_decomposition(std::size_t width, std::size_t height,
                                           int processes) {
    if (width == 0 || height == 0 || processes < 1) {
        throw std::invalid_argument("describe_decomposition: dimensiones o procesos invalidos");
    }
    DecompositionLayout layout;
    layout.width = width;
    layout.height = height;
    layout.processes = processes;
    layout.regions.reserve(static_cast<std::size_t>(processes));
    for (int r = 0; r < processes; ++r) {
        const auto part = partition_rows(height, processes, r);
        const auto nb = halo_neighbors(part);
        RankRegion reg;
        reg.rank = r;
        reg.firstRow = part.firstRow;
        reg.rowCount = part.rowCount;
        reg.localBufferCells = (width + 2) * (part.rowCount + 2);
        if (nb.hasUp()) {
            reg.top = HaloEdge{nb.up, part.firstRow - 1};
            ++layout.haloMessagesPerStep;  // este rank envia su primera fila hacia arriba
        }
        if (nb.hasDown()) {
            reg.bottom = HaloEdge{nb.down, part.endRow()};
            ++layout.haloMessagesPerStep;  // y su ultima fila hacia abajo
        }
        layout.regions.push_back(reg);
    }
    layout.haloBytesPerStep = layout.haloMessagesPerStep * width;
    return layout;
}

int owner_of_row(const DecompositionLayout& layout, std::size_t row) {
    for (const auto& reg : layout.regions) {
        if (!reg.empty() && row >= reg.firstRow && row < reg.endRow()) {
            return reg.rank;
        }
    }
    return kNoNeighbor;
}

void write_decomposition_csv(std::ostream& out, const DecompositionSnapshot& snap) {
    const auto& L = snap.layout;
    if (!snap.metrics.empty() && snap.metrics.size() != L.regions.size()) {
        throw std::invalid_argument("write_decomposition_csv: metricas y regiones no coinciden");
    }
    if (snap.haloMode.find_first_of(",\r\n") != std::string::npos) {
        throw std::invalid_argument("write_decomposition_csv: modo de halo no valido");
    }
    out << kMagic << '\n' << kGlobalHeader << '\n'
        << L.width << ',' << L.height << ',' << L.processes << ','
        << snap.generation << ',' << snap.haloMode << '\n'
        << kRankHeader << '\n';
    const auto edge = [&out](const HaloEdge& e) {
        out << e.sourceRank << ',';
        if (!e.isGlobalBorder()) {
            out << e.sourceGlobalRow;
        }
    };
    for (std::size_t i = 0; i < L.regions.size(); ++i) {
        const auto& reg = L.regions[i];
        out << reg.rank << ',' << reg.firstRow << ',' << reg.rowCount << ',';
        edge(reg.top);
        out << ',';
        edge(reg.bottom);
        out << ',' << reg.localBufferCells << ',';
        if (!snap.metrics.empty()) {
            const auto& m = snap.metrics[i];
            if (m.rank != reg.rank) {
                throw std::invalid_argument("write_decomposition_csv: metricas fuera de orden");
            }
            out << m.aliveCount << ',' << number(m.haloSeconds) << ','
                << number(m.computeSeconds) << ',' << m.messagesSent << ',' << m.bytesSent;
        } else {
            out << ",,,,";
        }
        out << '\n';
    }
    if (!out) {
        throw std::runtime_error("write_decomposition_csv: fallo de escritura");
    }
}

DecompositionSnapshot read_decomposition_csv(std::istream& in) {
    if (next_line(in, "la marca de version") != kMagic) {
        throw std::runtime_error("CSV de descomposicion: version no reconocida");
    }
    if (next_line(in, "la cabecera global") != kGlobalHeader) {
        throw std::runtime_error("CSV de descomposicion: cabecera global inesperada");
    }
    const auto g = split(next_line(in, "la fila global"));
    if (g.size() != 5) {
        throw std::runtime_error("CSV de descomposicion: fila global con columnas incorrectas");
    }
    DecompositionSnapshot snap;
    const auto width = parse<std::size_t>(g[0], "width");
    const auto height = parse<std::size_t>(g[1], "height");
    const auto processes = parse<int>(g[2], "processes");
    snap.generation = parse<std::size_t>(g[3], "generation");
    snap.haloMode = g[4];
    try {
        snap.layout = describe_decomposition(width, height, processes);
    } catch (const std::invalid_argument& e) {
        throw std::runtime_error(std::string("CSV de descomposicion: ") + e.what());
    }
    if (next_line(in, "la cabecera de ranks") != kRankHeader) {
        throw std::runtime_error("CSV de descomposicion: cabecera de ranks inesperada");
    }

    bool anyMetrics = false, allMetrics = true;
    std::vector<RankRunMetrics> metrics;
    for (const auto& expected : snap.layout.regions) {
        const auto f = split(next_line(in, "una fila de rank"));
        if (f.size() != 13) {
            throw std::runtime_error("CSV de descomposicion: fila de rank con columnas incorrectas");
        }
        RankRegion reg;
        reg.rank = parse<int>(f[0], "rank");
        reg.firstRow = parse<std::size_t>(f[1], "first_row");
        reg.rowCount = parse<std::size_t>(f[2], "row_count");
        reg.top.sourceRank = parse<int>(f[3], "halo_top_rank");
        if (!reg.top.isGlobalBorder()) reg.top.sourceGlobalRow = parse<std::size_t>(f[4], "halo_top_row");
        else if (!f[4].empty()) throw std::runtime_error("CSV de descomposicion: borde superior con fila");
        reg.bottom.sourceRank = parse<int>(f[5], "halo_bottom_rank");
        if (!reg.bottom.isGlobalBorder()) reg.bottom.sourceGlobalRow = parse<std::size_t>(f[6], "halo_bottom_row");
        else if (!f[6].empty()) throw std::runtime_error("CSV de descomposicion: borde inferior con fila");
        reg.localBufferCells = parse<std::size_t>(f[7], "local_buffer_cells");

        if (reg.rank != expected.rank || reg.firstRow != expected.firstRow ||
            reg.rowCount != expected.rowCount || !same_edge(reg.top, expected.top) ||
            !same_edge(reg.bottom, expected.bottom) ||
            reg.localBufferCells != expected.localBufferCells) {
            throw std::runtime_error("CSV de descomposicion: la region del rank " +
                                     std::to_string(expected.rank) +
                                     " no coincide con la particion");
        }

        const bool hasMetrics = !f[8].empty() || !f[9].empty() || !f[10].empty() ||
                                !f[11].empty() || !f[12].empty();
        anyMetrics = anyMetrics || hasMetrics;
        allMetrics = allMetrics && hasMetrics;
        if (hasMetrics) {
            RankRunMetrics m;
            m.rank = reg.rank;
            m.aliveCount = parse<std::size_t>(f[8], "alive");
            m.haloSeconds = parse<double>(f[9], "halo_seconds");
            m.computeSeconds = parse<double>(f[10], "compute_seconds");
            m.messagesSent = parse<std::size_t>(f[11], "messages_sent");
            m.bytesSent = parse<std::size_t>(f[12], "bytes_sent");
            metrics.push_back(m);
        }
    }
    if (anyMetrics && !allMetrics) {
        throw std::runtime_error("CSV de descomposicion: metricas incompletas");
    }
    std::string extra;
    while (std::getline(in, extra)) {
        if (!extra.empty() && extra != "\r") {
            throw std::runtime_error("CSV de descomposicion: datos adicionales");
        }
    }
    snap.metrics = std::move(metrics);
    return snap;
}

} // namespace conway::mpi

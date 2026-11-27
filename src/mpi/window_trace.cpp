#include "mpi/window_trace.hpp"
#include "mpi/counts.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace conway::mpi {
namespace {
std::size_t number(const std::string& text) {
    std::size_t value{};
    const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (status != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error("Número inválido en la captura.");
    return value;
}
}

std::string encodeWindowTrace(const WindowTrace& t) {
    std::ostringstream out;
    out << t.selection.generation << '\t' << t.selection.rank << '\t' << t.selection.row << '\t'
        << t.selection.column << '\t' << t.width << '\t' << t.height << '\t'
        << t.rows << '\t' << t.columns << '\t' << t.aliveBefore << '\t' << t.aliveAfter << '\t'
        << t.before << '\t' << t.after << '\t' << t.halos[0] << '\t' << t.halos[1] << '\t';
    if (t.probe) {
        out << static_cast<int>(t.probe->before);
        for (auto neighbor : t.probe->neighbors) out << static_cast<int>(neighbor);
        out << static_cast<int>(t.probe->neighborCount) << static_cast<int>(t.probe->after);
    }
    return out.str();
}

WindowTrace decodeWindowTrace(const std::vector<std::string>& fields) {
    if (fields.size() != 15) throw std::runtime_error("Captura de ventana incompleta.");
    WindowTrace t;
    const auto rank = number(fields[1]);
    if (rank > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Rank de captura inválido.");
    t.selection = {number(fields[0]), number(fields[2]), number(fields[3]), static_cast<int>(rank)};
    t.width = number(fields[4]);
    t.height = number(fields[5]);
    t.rows = number(fields[6]);
    t.columns = number(fields[7]);
    t.aliveBefore = number(fields[8]);
    t.aliveAfter = number(fields[9]);
    const auto cells = checked_cell_count(t.width, t.height);
    const auto binary = [](const std::string& s, std::size_t length) {
        return s.size() == length && s.find_first_not_of("01") == s.npos;
    };
    if (cells > windowInputCells || t.selection.generation >= std::numeric_limits<std::size_t>::max() ||
        t.selection.row >= t.height || t.selection.column >= t.width ||
        t.rows != std::min(windowRows, t.height - t.selection.row) ||
        t.columns != std::min(windowColumns, t.width - t.selection.column) ||
        t.aliveBefore > cells || t.aliveAfter > cells ||
        !binary(fields[10], t.rows * t.columns) || !binary(fields[11], t.rows * t.columns) ||
        (!fields[12].empty() && !binary(fields[12], t.columns)) ||
        (!fields[13].empty() && !binary(fields[13], t.columns)))
        throw std::runtime_error("Dimensiones o celdas inválidas en la captura.");

    t.before = fields[10];
    t.after = fields[11];
    t.halos = {fields[12], fields[13]};
    const auto& probe = fields[14];
    if (!probe.empty()) {
        if (probe.size() != 11 || !binary(probe.substr(0, 9), 9) || !binary(probe.substr(10), 1) ||
            probe[9] - '0' != std::count(probe.begin() + 1, probe.begin() + 9, '1'))
            throw std::runtime_error("Sonda de captura inválida.");
        core::CellEvaluation evaluation;
        evaluation.before = probe[0] - '0';
        for (std::size_t i = 0; i < 8; ++i) evaluation.neighbors[i] = probe[i + 1] - '0';
        evaluation.neighborCount = probe[9] - '0';
        evaluation.after = probe[10] - '0';
        t.probe = evaluation;
    }
    return t;
}
}

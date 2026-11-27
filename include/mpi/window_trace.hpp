#pragma once

#include "core/cell_probe.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace conway::mpi {

inline constexpr std::size_t windowRows = 16, windowColumns = 16;
inline constexpr std::size_t windowInputCells = 64u * 1024u * 1024u - 32u;

struct WindowRequest {
    std::size_t generation{}, row{}, column{};
    int rank{};
    bool operator==(const WindowRequest&) const = default;
};

struct WindowTrace {
    WindowRequest selection;
    std::size_t width{}, height{}, rows{}, columns{}, aliveBefore{}, aliveAfter{};
    std::string before, after; // Row-major cells inside the global window.
    std::array<std::string, 2> halos; // Received segments in Sendrecv order. Empty if outside the window.
    std::optional<core::CellEvaluation> probe;
};

// Results use tab-separated ASCII fields, with a single byte per observed cell.
std::string encodeWindowTrace(const WindowTrace& trace);
WindowTrace decodeWindowTrace(const std::vector<std::string>& fields);

}  // namespace conway::mpi

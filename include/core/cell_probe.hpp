#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace conway::core {
// Values read by the shared B3/S23 evaluation, in NW,N,NE,W,E,SW,S,SE order.
// No second evaluation or UI reconstruction of the rule is needed.
struct CellEvaluation {
    std::uint8_t before{};
    std::array<std::uint8_t,8> neighbors{};
    std::uint8_t neighborCount{}, after{};
};
// Logical coordinates in the LOCAL padded traversal (own rows/columns start at 1).
struct CellProbe {
    std::size_t row{}, column{};
    bool captured{false};
    CellEvaluation evaluation;
};
}

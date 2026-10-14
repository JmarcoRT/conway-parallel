#include "mpi/partition.hpp"

#include <algorithm>
#include <stdexcept>

namespace conway::mpi {

RowPartition partition_rows(std::size_t globalHeight, int size, int rank) {
    if (size < 1) {
        throw std::invalid_argument("partition_rows: size debe ser >= 1");
    }
    if (rank < 0 || rank >= size) {
        throw std::invalid_argument("partition_rows: rank fuera de [0, size)");
    }

    const std::size_t p     = static_cast<std::size_t>(size);
    const std::size_t r     = static_cast<std::size_t>(rank);
    const std::size_t base  = globalHeight / p;
    const std::size_t extra = globalHeight % p;

    RowPartition part;
    part.rank         = rank;
    part.size         = size;
    part.globalHeight = globalHeight;
    part.rowCount     = base + (r < extra ? 1 : 0);
    part.firstRow     = r * base + std::min(r, extra);
    return part;
}

} // namespace conway::mpi

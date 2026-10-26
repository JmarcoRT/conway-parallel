#include "mpi/halo.hpp"

namespace conway::mpi {

HaloNeighbors halo_neighbors(const RowPartition& part) {
    HaloNeighbors n;
    if (part.empty()) {
        return n;
    }
    if (part.rank > 0) {
        const auto above = partition_rows(part.globalHeight, part.size, part.rank - 1);
        if (!above.empty() && above.endRow() == part.firstRow) {
            n.up = above.rank;
        }
    }
    if (part.rank + 1 < part.size) {
        const auto below = partition_rows(part.globalHeight, part.size, part.rank + 1);
        if (!below.empty() && below.firstRow == part.endRow()) {
            n.down = below.rank;
        }
    }
    return n;
}

} // namespace conway::mpi

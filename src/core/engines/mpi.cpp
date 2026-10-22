#include "core/engines/mpi.hpp"
#include "core/engines/sequential.hpp"

#include <algorithm>
#include <climits>
#include <limits>
#include <stdexcept>

namespace conway::core {

std::size_t step_mpi(GameState& s, const mpi::RowPartition& part, MPI_Comm comm) {
    // Validate collectively before any point-to-point exchange. Allocation and
    // complete input validation belong to initialization, not the step loop.
    int rank = 0, size = 0;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    const auto expected = mpi::partition_rows(part.globalHeight, size, rank);
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const bool shape = part.globalHeight > 0 && s.width > 0 &&
        s.width <= static_cast<std::size_t>(INT_MAX) / part.globalHeight &&
        s.height <= maximum - 2 && s.stride == s.width + 2 &&
        s.stride <= maximum / (s.height + 2);
    int valid = shape && part.rank == rank && part.size == size &&
        part.firstRow == expected.firstRow && part.rowCount == expected.rowCount &&
        s.height == part.rowCount && s.current.size() == s.stride * (s.height + 2) &&
        s.next.size() == s.current.size() && s.generation < maximum;
    int allValid = 0;
    MPI_Allreduce(&valid, &allValid, 1, MPI_INT, MPI_MIN, comm);
    if (!allValid) throw std::invalid_argument("step_mpi: estado o particion incompatible");

    // MPI_PROC_NULL leaves receive buffers untouched: explicitly clear them on
    // every step, including after swapping buffers from earlier generations.
    std::fill_n(s.current.data(), s.stride, 0);
    std::fill_n(s.current.data() + (s.height + 1) * s.stride, s.stride, 0);
    if (!part.empty()) {
        const int above = part.firstRow == 0 ? MPI_PROC_NULL : rank - 1;
        const int below = part.endRow() == part.globalHeight ? MPI_PROC_NULL : rank + 1;
        const int count = static_cast<int>(s.width);
        MPI_Sendrecv(s.current.data() + s.stride + 1, count, MPI_UINT8_T, above, 0,
                     s.current.data() + (s.height + 1) * s.stride + 1,
                     count, MPI_UINT8_T, below, 0, comm, MPI_STATUS_IGNORE);
        MPI_Sendrecv(s.current.data() + s.height * s.stride + 1,
                     count, MPI_UINT8_T, below, 1,
                     s.current.data() + 1, count, MPI_UINT8_T, above, 1,
                     comm, MPI_STATUS_IGNORE);
    }
    step_sequential(s);
    const unsigned long long local = s.aliveCount;
    unsigned long long global = 0;
    MPI_Allreduce(&local, &global, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, comm);
    return static_cast<std::size_t>(global);
}

} // namespace conway::core

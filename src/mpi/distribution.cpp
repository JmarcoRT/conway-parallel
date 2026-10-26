#include "mpi/distribution.hpp"

#include "mpi/local_state.hpp"

#include <climits>
#include <stdexcept>

namespace conway::mpi {

namespace {

void counts_and_displs(std::size_t width, std::size_t height, int size,
                       std::vector<int>& counts, std::vector<int>& displs) {
    counts.resize(static_cast<std::size_t>(size));
    displs.resize(static_cast<std::size_t>(size));
    for (int r = 0; r < size; ++r) {
        const auto part = partition_rows(height, size, r);
        counts[static_cast<std::size_t>(r)] = static_cast<int>(part.rowCount * width);
        displs[static_cast<std::size_t>(r)] = static_cast<int>(part.firstRow * width);
    }
}

} // namespace

void require_int_counts(std::size_t width, std::size_t height) {
    if (width == 0 || height == 0 || width > static_cast<std::size_t>(INT_MAX) ||
        height > static_cast<std::size_t>(INT_MAX) / width) {
        throw std::length_error("tablero demasiado grande para los conteos int de MPI");
    }
}

core::GameState scatter_board(const InputData& input, std::size_t width,
                              std::size_t height, int root, MPI_Comm comm) {
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    const auto part = partition_rows(height, size, rank);

    std::vector<int> counts, displs;
    if (rank == root) {
        if (input.width != width || input.height != height ||
            input.cells.size() != width * height) {
            throw std::invalid_argument("scatter_board: entrada inconsistente en root");
        }
        counts_and_displs(width, height, size, counts, displs);
    }
    std::vector<std::uint8_t> rows(part.rowCount * width);
    MPI_Scatterv(rank == root ? input.cells.data() : nullptr, counts.data(), displs.data(),
                 MPI_UNSIGNED_CHAR, rows.data(), static_cast<int>(rows.size()),
                 MPI_UNSIGNED_CHAR, root, comm);
    return make_local_state_from_rows(width, part, rows);
}

std::vector<std::uint8_t> gather_board(const core::GameState& local,
                                       std::size_t height, int root, MPI_Comm comm) {
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    const std::vector<std::uint8_t> mine = pack_local_rows(local);
    std::vector<int> counts, displs;
    std::vector<std::uint8_t> board;
    if (rank == root) {
        counts_and_displs(local.width, height, size, counts, displs);
        board.assign(local.width * height, 0);
    }
    MPI_Gatherv(mine.data(), static_cast<int>(mine.size()), MPI_UNSIGNED_CHAR,
                board.data(), counts.data(), displs.data(), MPI_UNSIGNED_CHAR,
                root, comm);
    return board;
}

} // namespace conway::mpi

#include "core/engines/mpi.hpp"
#include "core/engines/sequential.hpp"
#include "input/input.hpp"
#include "mpi/local_state.hpp"
#include "mpi/counts.hpp"
#include <mpi.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Same distribution/reassembly path as mpi_partition_prototype. The reference
// is a full-board GameState advanced only by the existing sequential engine.
bool check_case(const InputData& input, const std::string& name,
                int rank, int size, MPI_Comm comm) {
    (void)conway::mpi::checked_cell_count(input.width, input.height);
    const auto part = conway::mpi::partition_rows(input.height, size, rank);
    std::vector<int> counts(size), offsets(size);
    for (int r = 0; r < size; ++r) {
        const auto p = conway::mpi::partition_rows(input.height, size, r);
        counts[r] = static_cast<int>(p.rowCount * input.width);
        offsets[r] = static_cast<int>(p.firstRow * input.width);
    }
    std::vector<std::uint8_t> received(part.rowCount * input.width);
    MPI_Scatterv(input.cells.data(), counts.data(), offsets.data(), MPI_UINT8_T,
                 received.data(), static_cast<int>(received.size()), MPI_UINT8_T, 0, comm);
    auto local = conway::mpi::make_local_state_from_rows(input.width, part, received);
    auto reference = conway::mpi::make_local_state(
        input, conway::mpi::partition_rows(input.height, 1, 0));
    std::vector<std::uint8_t> board(input.cells.size());
    std::size_t alive = reference.aliveCount;
    bool passed = true;
    for (std::size_t generation = 0; generation <= 30; ++generation) {
        if (generation) {
            alive = conway::core::step_mpi(local, part, comm);
            if (rank == 0) conway::core::step_sequential(reference);
        }
        const auto rows = conway::mpi::pack_local_rows(local);
        MPI_Gatherv(rows.data(), static_cast<int>(rows.size()), MPI_UINT8_T,
                    board.data(), counts.data(), offsets.data(), MPI_UINT8_T, 0, comm);
        // Verify local invariants on EVERY rank, including empty ranks. The
        // global population returned by step_mpi must not replace local counts.
        const auto localAlive = static_cast<std::size_t>(
            std::count(rows.begin(), rows.end(), std::uint8_t{1}));
        int ok = local.generation == generation && local.aliveCount == localAlive;
        for (const auto* buffer : {&local.current, &local.next}) {
            for (std::size_t r = 0; r < local.height + 2; ++r)
                ok = ok && (*buffer)[r * local.stride] == 0 &&
                    (*buffer)[r * local.stride + local.width + 1] == 0;
            for (std::size_t c = 0; c < local.stride; ++c) {
                if (part.empty() || part.firstRow == 0)
                    ok = ok && (*buffer)[c] == 0;
                if (part.empty() || part.endRow() == part.globalHeight)
                    ok = ok && (*buffer)[(local.height + 1) * local.stride + c] == 0;
            }
        }
        if (rank == 0) {
            ok = ok && board == conway::mpi::pack_local_rows(reference) &&
                reference.generation == generation && alive == reference.aliveCount;
        }
        int allOk = 0;
        MPI_Allreduce(&ok, &allOk, 1, MPI_INT, MPI_MIN, comm);
        if (!allOk) {
            if (rank == 0) std::cerr << "FAIL " << name << " generation=" << generation << '\n';
            passed = false;
            break;
        }
    }
    if (rank == 0 && passed) std::cout << "PASS " << name << " P=" << size << " steps=30\n";
    return passed;
}
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    int status = 0;
    try {
        if (argc != 2) throw std::invalid_argument("expected repository path");
        bool ok = true;
        for (const char* pattern : {"block", "blinker", "glider", "toad", "beacon", "pulsar"}) {
            const auto input = loadInput(std::string(argv[1]) + "/patterns/" + pattern + ".txt");
            ok = check_case(input, pattern, rank, size, MPI_COMM_WORLD) && ok;
        }
        for (std::size_t h : {1u, 2u, 3u, 5u, 10u, 17u}) {
            for (std::size_t w : {1u, 2u, 7u, 13u}) {
                for (int mode = 0; mode < 3; ++mode) {
                    InputData input{w, h, std::vector<std::uint8_t>(w * h)};
                    for (std::size_t r = 0; r < h; ++r)
                        for (std::size_t c = 0; c < w; ++c)
                            input.cells[r * w + c] = mode == 0 ? 0 : mode == 1 ? 1 :
                                ((r * 7 + c * 3 + r * c) % 5 == 0);
                    const auto name = std::to_string(h) + "x" + std::to_string(w) +
                        " mode=" + std::to_string(mode);
                    ok = check_case(input, name, rank, size, MPI_COMM_WORLD) && ok;
                }
            }
        }
        // A vertical blinker centered on each rank boundary must exchange both
        // directions correctly. It also detects stale halos after buffer swaps.
        for (int boundary = 1; boundary < std::min(size, 11); ++boundary) {
            InputData input{7, 11, std::vector<std::uint8_t>(77)};
            const auto row = conway::mpi::partition_rows(11, size, boundary).firstRow;
            if (row > 0 && row + 1 < input.height) {
                for (std::size_t r = row - 1; r <= row + 1; ++r) input.cells[r * 7 + 3] = 1;
                ok = check_case(input, "boundary=" + std::to_string(row), rank, size, MPI_COMM_WORLD) && ok;
            }
        }
        // One rank rejects a mismatching partition: every rank must leave the
        // collective validation together instead of stranding its neighbours.
        InputData input{3, 3, std::vector<std::uint8_t>(9)};
        auto part = conway::mpi::partition_rows(3, size, rank);
        auto state = conway::mpi::make_local_state(input, part);
        if (rank == 0) ++part.firstRow;
        int caught = 0;
        try { conway::core::step_mpi(state, part, MPI_COMM_WORLD); }
        catch (const std::invalid_argument&) { caught = 1; }
        int allCaught = 0;
        MPI_Allreduce(&caught, &allCaught, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        status = ok && allCaught ? 0 : 1;
        if (rank == 0) std::cout << "MPI evolution: " << (status ? "FAIL" : "OK") << '\n';
    } catch (const std::exception& error) {
        std::cerr << "rank " << rank << ": " << error.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 2);
        return 2;
    }
    MPI_Finalize();
    return status;
}

#include "cluster_didactic.hpp"

#include "core/engines/mpi.hpp"
#include "mpi/counts.hpp"
#include "mpi/local_state.hpp"
#include "mpi/window_trace.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using namespace conway;
constexpr auto windowCells = mpi::windowRows * mpi::windowColumns;
constexpr auto packetSize = 2 * windowCells + 2 * mpi::windowColumns + 11;
constexpr std::size_t distributionBlockBytes = 4096;
constexpr int distributionDataTag = 20, distributionAckTag = 21;
enum Operation : std::uint64_t { Close, Load, Reset, Step };

InputData read_input(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    InputData result{};
    if (!(input >> result.height >> result.width) || input.get() != '\n')
        throw std::runtime_error("No se pudo leer la entrada de la sesión.");
    const auto cells = mpi::checked_cell_count(result.width, result.height);
    if (cells > mpi::windowInputCells) throw std::runtime_error("La entrada supera el límite MPI.");
    result.cells.resize(cells);
    if (!input.read(reinterpret_cast<char*>(result.cells.data()), cells) || input.peek() != EOF ||
        std::any_of(result.cells.begin(), result.cells.end(), [](auto cell) { return cell > 1; }))
        throw std::runtime_error("Entrada incompleta o no binaria.");
    return result;
}

bool all_ready(bool valid, MPI_Comm comm) {
    int local = valid, global = 0;
    MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_MIN, comm);
    return global != 0;
}

void trace_distribution(const char* phase, const mpi::RowPartition& part, std::size_t width,
                        std::size_t sendSize, std::size_t receiveSize, MPI_Comm comm) {
    if (!std::getenv("CONWAY_CLUSTER_TRACE_TRANSFER")) return;
    int inter = 0, typeSize = 0;
    MPI_Comm_test_inter(comm, &inter);
    MPI_Type_size(MPI_UINT8_T, &typeSize);
    std::clog << "[reparto " << phase << "] rank=" << part.rank << " P=" << part.size
              << " comm=" << (inter ? "inter" : "intra") << " root=0"
              << " MPI_UINT8_T=" << typeSize << "B ancho=" << width << " alto=" << part.globalHeight
              << " filas=" << part.rowCount << " offset=" << part.firstRow * width
              << " origen=" << sendSize << "B destino=" << receiveSize << "B bloque="
              << distributionBlockBytes << "B";
    if (part.rank == 0) {
        for (int peer = 0; peer < part.size; ++peer) {
            const auto slice = mpi::partition_rows(part.globalHeight, part.size, peer);
            std::clog << " R" << peer << '=' << slice.firstRow * width << '+' << slice.rowCount * width;
        }
    }
    std::clog << std::endl;
}

void transfer_rows(const InputData& input, std::vector<std::uint8_t>& rows,
                   const mpi::RowPartition& part, std::size_t width, MPI_Comm comm) {
    trace_distribution("inicio", part, width, input.cells.size(), rows.size(), comm);
    if (part.rank == 0) {
        std::copy_n(input.cells.data(), rows.size(), rows.data());
        for (int peer = 1; peer < part.size; ++peer) {
            const auto slice = mpi::partition_rows(part.globalHeight, part.size, peer);
            const auto* data = input.cells.data() + slice.firstRow * width;
            const auto bytes = slice.rowCount * width;
            for (std::size_t offset = 0; offset < bytes; offset += distributionBlockBytes) {
                const auto count = static_cast<int>(std::min(distributionBlockBytes, bytes - offset));
                // Keep at most one data block outstanding, even with eager sends.
                MPI_Sendrecv(data + offset, count, MPI_UINT8_T, peer, distributionDataTag,
                             nullptr, 0, MPI_UINT8_T, peer, distributionAckTag, comm, MPI_STATUS_IGNORE);
            }
        }
    } else {
        for (std::size_t offset = 0; offset < rows.size(); offset += distributionBlockBytes) {
            const auto count = static_cast<int>(std::min(distributionBlockBytes, rows.size() - offset));
            MPI_Status status;
            MPI_Recv(rows.data() + offset, count, MPI_UINT8_T, 0, distributionDataTag, comm, &status);
            int received = 0;
            MPI_Get_count(&status, MPI_UINT8_T, &received);
            if (received != count) throw std::runtime_error("Bloque de reparto MPI incompleto.");
            MPI_Send(nullptr, 0, MPI_UINT8_T, 0, distributionAckTag, comm);
        }
    }
    trace_distribution("fin", part, width, input.cells.size(), rows.size(), comm);
}

bool distribute(const InputData& input, MPI_Comm comm, int rank, int size,
                mpi::RowPartition& partition, core::GameState& state, std::size_t& population) {
    std::array<std::uint64_t, 2> shape{input.width, input.height};
    MPI_Bcast(shape.data(), shape.size(), MPI_UINT64_T, 0, comm);
    const auto [width, height] = shape;
    auto part = mpi::partition_rows(height, size, rank);
    std::vector<std::uint8_t> rows;
    bool ready = true;
    try { rows.resize(part.rowCount * width); }
    catch (const std::exception&) { ready = false; }
    if (!all_ready(ready, comm)) return false;
    transfer_rows(input, rows, part, width, comm);

    core::GameState replacement{};
    try { replacement = mpi::make_local_state_from_rows(width, part, rows); }
    catch (const std::exception&) { ready = false; }
    if (!all_ready(ready, comm)) return false;

    const std::uint64_t local = replacement.aliveCount;
    std::uint64_t total = 0;
    MPI_Allreduce(&local, &total, 1, MPI_UINT64_T, MPI_SUM, comm);
    population = total;
    state = std::move(replacement);
    partition = part;
    return true;
}

void capture(core::GameState& state, const mpi::RowPartition& part, MPI_Comm comm,
             const mpi::WindowRequest& request, std::size_t& population, std::uint64_t id) {
    const auto rows = std::min(mpi::windowRows, part.globalHeight - request.row);
    const auto columns = std::min(mpi::windowColumns, state.width - request.column);
    const auto first = std::max(request.row, part.firstRow);
    const auto end = std::min(request.row + rows, part.endRow());
    using Packet = std::array<std::uint8_t, packetSize>;
    Packet local{}, global{};

    const auto copy_rows = [&](std::size_t base) {
        for (auto row = first; row < end; ++row) {
            const auto from = (row - part.firstRow + 1) * state.stride + request.column + 1;
            const auto to = base + (row - request.row) * columns;
            std::copy_n(state.current.data() + from, columns, local.data() + to);
        }
    };
    copy_rows(0);

    core::CellProbe cell{};
    core::MpiWindowProbe observer{request.column, {}, nullptr};
    if (part.rank == request.rank && first < end) {
        cell.row = end - part.firstRow;
        cell.column = request.column + columns / 2 + 1;
        observer.cell = &cell;
        if (end == part.endRow())
            observer.received[0] = std::span(local).subspan(2 * windowCells, columns);
        if (first == part.firstRow)
            observer.received[1] = std::span(local).subspan(2 * windowCells + mpi::windowColumns, columns);
    }
    const auto before = population;
    population = core::step_mpi(state, part, comm, nullptr, observer.cell ? &observer : nullptr);
    copy_rows(windowCells);
    if (cell.captured) {
        auto data = local.end() - 11;
        *data++ = cell.evaluation.before;
        data = std::copy(cell.evaluation.neighbors.begin(), cell.evaluation.neighbors.end(), data);
        *data++ = cell.evaluation.neighborCount;
        *data = cell.evaluation.after;
    }
    // Each cell and metadata slot has exactly one owner; zero is the neutral
    // value for ranks without rows in the requested window.
    MPI_Reduce(local.data(), global.data(), global.size(), MPI_UINT8_T, MPI_MAX, 0, comm);
    if (part.rank != 0) return;

    mpi::WindowTrace trace{};
    trace.selection = request;
    trace.width = state.width;
    trace.height = part.globalHeight;
    trace.rows = rows;
    trace.columns = columns;
    trace.aliveBefore = before;
    trace.aliveAfter = population;
    const auto digits = [&](std::size_t start, std::size_t length) {
        std::string result(length, '0');
        for (std::size_t i = 0; i < length; ++i) result[i] += global[start + i];
        return result;
    };
    trace.before = digits(0, rows * columns);
    trace.after = digits(windowCells, rows * columns);

    const auto selected = mpi::partition_rows(part.globalHeight, part.size, request.rank);
    const auto lo = std::max(request.row, selected.firstRow);
    const auto hi = std::min(request.row + rows, selected.endRow());
    if (lo < hi) {
        if (hi == selected.endRow()) trace.halos[0] = digits(2 * windowCells, columns);
        if (lo == selected.firstRow) trace.halos[1] = digits(2 * windowCells + mpi::windowColumns, columns);
        core::CellEvaluation evaluation;
        auto data = global.end() - 11;
        evaluation.before = *data++;
        std::copy_n(data, 8, evaluation.neighbors.begin());
        evaluation.neighborCount = data[8];
        evaluation.after = data[9];
        trace.probe = evaluation;
    }
    std::cout << "TRACE\t" << id << '\t' << mpi::encodeWindowTrace(trace) << std::endl;
}
}

void run_cluster_didactic(MPI_Comm comm, const std::filesystem::path& input_path) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    InputData initial{};
    conway::mpi::RowPartition part{};
    conway::core::GameState state{};
    std::size_t population = 0;
    for (;;) {
        std::array<std::uint64_t, 6> command{};
        InputData candidate{};
        if (rank == 0) {
            std::string line, operation;
            if (std::getline(std::cin, line) && line != "CLOSE") {
                try {
                    std::istringstream input(line);
                    if (!(input >> operation >> command[1])) throw std::runtime_error("Orden inválida.");
                    if (operation == "LOAD") {
                        candidate = read_input(input_path);
                        command[0] = Load;
                    } else if (operation == "RESET" && state.width) {
                        command[0] = Reset;
                    } else if (operation == "STEP" && state.width &&
                               (input >> command[2] >> command[3] >> command[4] >> command[5]) &&
                               command[2] == state.generation &&
                               command[2] < std::numeric_limits<std::size_t>::max() &&
                               command[3] < static_cast<std::uint64_t>(size) &&
                               command[4] < part.globalHeight && command[5] < state.width) {
                        command[0] = Step;
                    } else throw std::runtime_error("Operación, generación o ventana inválida.");
                } catch (const std::exception& error) {
                    std::cout << "REJECT\t" << command[1] << '\t' << error.what() << std::endl;
                    continue;
                }
            }
        }
        MPI_Bcast(command.data(), command.size(), MPI_UINT64_T, 0, comm);
        const auto [operation, id, generation, selected, row, column] = command;
        if (operation == Close) break;
        if (operation == Step) {
            capture(state, part, comm, {generation, row, column, static_cast<int>(selected)}, population, id);
        } else {
            const auto& input = operation == Load ? candidate : initial;
            if (distribute(input, comm, rank, size, part, state, population)) {
                if (operation == Load && rank == 0) initial = std::move(candidate);
                if (rank == 0) std::cout << "LOADED\t" << id << '\t' << state.width << '\t'
                    << part.globalHeight << '\t' << population << std::endl;
            } else if (rank == 0) {
                std::cout << "REJECT\t" << id << "\tNo se pudo reservar el estado MPI." << std::endl;
            }
        }
    }
    MPI_Barrier(comm);
}

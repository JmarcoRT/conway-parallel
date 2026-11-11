// Prueba de integracion: forma el cluster sin SSH y ejecuta la evolucion
// MPI de Conway sobre el comunicador resultante.
//
// Servidor:
//   ./conway_cluster --server --clients 3 --plan plan.json \
//       --pattern patterns/glider.txt --steps 30
//
// Cliente:
//   ./conway_cluster --client --id 1 --clients 3 --port-name '...' \
//       --pattern patterns/glider.txt --steps 30

#include "core/engines/mpi.hpp"
#include "core/engines/sequential.hpp"
#include "input/input.hpp"
#include "mpi/counts.hpp"
#include "mpi/local_state.hpp"
#include "mpi/partition.hpp"

#include <mpi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    bool server{false};
    int clients{0};
    int id{0};
    std::string plan;
    std::string port_name;
    std::string pattern{"patterns/glider.txt"};
    std::string metrics_path;
    std::string board_path;
    int steps{30};
};

bool has_flag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

std::string get_arg(int argc, char** argv, const char* key) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
    }
    return {};
}

Options parse_options(int argc, char** argv) {
    Options o;
    o.server = has_flag(argc, argv, "--server");
    o.clients = std::atoi(get_arg(argc, argv, "--clients").c_str());
    o.id = std::atoi(get_arg(argc, argv, "--id").c_str());
    o.plan = get_arg(argc, argv, "--plan");
    o.port_name = get_arg(argc, argv, "--port-name");
    const auto pattern = get_arg(argc, argv, "--pattern");
    if (!pattern.empty()) o.pattern = pattern;
    o.metrics_path = get_arg(argc, argv, "--metrics");
    o.board_path = get_arg(argc, argv, "--board");
    const auto steps = get_arg(argc, argv, "--steps");
    if (!steps.empty()) o.steps = std::atoi(steps.c_str());
    if (o.clients <= 0 || o.steps < 0) {
        throw std::invalid_argument("--clients debe ser > 0 y --steps >= 0");
    }
    return o;
}

MPI_Comm server_comm(const Options& o) {
    if (o.plan.empty()) throw std::invalid_argument("falta --plan");

    std::vector<std::string> ports(o.clients);
    FILE* f = std::fopen(o.plan.c_str(), "w");
    if (!f) throw std::runtime_error("no se pudo escribir el plan");
    std::fprintf(f, "{\n  \"nodes\": %d,\n  \"clients\": {\n", o.clients + 1);
    for (int i = 0; i < o.clients; ++i) {
        char port[MPI_MAX_PORT_NAME];
        if (MPI_Open_port(MPI_INFO_NULL, port) != MPI_SUCCESS) {
            std::fclose(f);
            throw std::runtime_error("MPI_Open_port fallo");
        }
        ports[i] = port;
        std::fprintf(f, "    \"%d\": {\"port_name\": \"%s\"}%s\n",
                     i + 1, port, (i + 1 < o.clients) ? "," : "");
        std::fflush(f);
    }
    std::fprintf(f, "  }\n}\n");
    std::fclose(f);

    MPI_Comm comm = MPI_COMM_SELF;
    for (int step = 0; step < o.clients; ++step) {
        MPI_Comm intercomm = MPI_COMM_NULL;
        MPI_Comm_accept(ports[step].c_str(), MPI_INFO_NULL, 0, comm,
                        &intercomm);
        MPI_Comm next = MPI_COMM_NULL;
        MPI_Intercomm_merge(intercomm, 0, &next);
        MPI_Comm_free(&intercomm);
        if (comm != MPI_COMM_SELF) MPI_Comm_free(&comm);
        comm = next;
    }
    return comm;
}

MPI_Comm client_comm(const Options& o) {
    if (o.id <= 0 || o.port_name.empty()) {
        throw std::invalid_argument("faltan --id o --port-name");
    }
    MPI_Comm intercomm = MPI_COMM_NULL;
    MPI_Comm_connect(o.port_name.c_str(), MPI_INFO_NULL, 0, MPI_COMM_SELF,
                     &intercomm);
    MPI_Comm comm = MPI_COMM_NULL;
    MPI_Intercomm_merge(intercomm, 1, &comm);
    MPI_Comm_free(&intercomm);

    for (int step = o.id; step < o.clients; ++step) {
        MPI_Comm next_inter = MPI_COMM_NULL;
        char dummy[1] = {0};
        MPI_Comm_accept(dummy, MPI_INFO_NULL, 0, comm, &next_inter);
        MPI_Comm next = MPI_COMM_NULL;
        MPI_Intercomm_merge(next_inter, 0, &next);
        MPI_Comm_free(&next_inter);
        MPI_Comm_free(&comm);
        comm = next;
    }
    return comm;
}

int check_conway(const Options& o, MPI_Comm comm) {
    int rank = 0;
    int size = 0;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    InputData input{0, 0, {}};
    if (rank == 0) input = loadInput(o.pattern);

    int width = 0;
    int height = 0;
    if (rank == 0) {
        width = static_cast<int>(input.width);
        height = static_cast<int>(input.height);
    }
    MPI_Bcast(&width, 1, MPI_INT, 0, comm);
    MPI_Bcast(&height, 1, MPI_INT, 0, comm);

    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("tablero vacio");
    }
    conway::mpi::checked_cell_count(static_cast<std::size_t>(width),
                                   static_cast<std::size_t>(height));

    if (rank != 0) {
        input.width = static_cast<std::size_t>(width);
        input.height = static_cast<std::size_t>(height);
        input.cells.assign(static_cast<std::size_t>(width) * height, 0);
    }

    const auto part = conway::mpi::partition_rows(
        static_cast<std::size_t>(height), size, rank);
    std::vector<int> counts(size), offsets(size);
    for (int r = 0; r < size; ++r) {
        const auto p = conway::mpi::partition_rows(
            static_cast<std::size_t>(height), size, r);
        counts[r] = static_cast<int>(p.rowCount * width);
        offsets[r] = static_cast<int>(p.firstRow * width);
    }

    std::vector<std::uint8_t> received(part.rowCount * width);
    MPI_Scatterv(rank == 0 ? input.cells.data() : nullptr,
                 counts.data(), offsets.data(), MPI_UINT8_T,
                 received.data(), static_cast<int>(received.size()),
                 MPI_UINT8_T, 0, comm);
    auto local = conway::mpi::make_local_state_from_rows(
        static_cast<std::size_t>(width), part, received);

    conway::core::GameState reference{};
    if (rank == 0) {
        reference = conway::mpi::make_local_state(
            input, conway::mpi::partition_rows(input.height, 1, 0));
    }

    std::vector<std::uint8_t> board(input.cells.size());
    std::size_t alive = rank == 0 ? reference.aliveCount : 0;
    bool ok = true;
    const double start_time = MPI_Wtime();

    for (int generation = 0; generation <= o.steps; ++generation) {
        if (generation > 0) {
            alive = conway::core::step_mpi(local, part, comm);
            if (rank == 0) conway::core::step_sequential(reference);
        }

        const auto rows = conway::mpi::pack_local_rows(local);
        MPI_Gatherv(rows.data(), static_cast<int>(rows.size()), MPI_UINT8_T,
                    board.data(), counts.data(), offsets.data(), MPI_UINT8_T,
                    0, comm);

        int local_ok = local.generation == static_cast<std::size_t>(generation);
        if (rank == 0) {
            local_ok = local_ok &&
                board == conway::mpi::pack_local_rows(reference) &&
                reference.generation == static_cast<std::size_t>(generation) &&
                alive == reference.aliveCount;
            if (!local_ok) {
                std::fprintf(stderr, "FAIL generacion %d\n", generation);
            }
        }
        int all_ok = 0;
        MPI_Allreduce(&local_ok, &all_ok, 1, MPI_INT, MPI_MIN, comm);
        if (!all_ok) {
            ok = false;
            break;
        }
    }

    const double elapsed = MPI_Wtime() - start_time;
    const unsigned long long local_alive =
        static_cast<unsigned long long>(local.aliveCount);
    std::vector<double> rank_seconds(static_cast<size_t>(size), 0.0);
    std::vector<unsigned long long> rank_alive(static_cast<size_t>(size), 0);
    MPI_Gather(&elapsed, 1, MPI_DOUBLE, rank_seconds.data(), 1, MPI_DOUBLE,
               0, comm);
    MPI_Gather(&local_alive, 1, MPI_UNSIGNED_LONG_LONG, rank_alive.data(), 1,
               MPI_UNSIGNED_LONG_LONG, 0, comm);

    if (rank == 0) {
        if (!o.metrics_path.empty()) {
            std::ofstream metrics(o.metrics_path);
            metrics << "rank,first_row,rows,alive,seconds\n";
            for (int r = 0; r < size; ++r) {
                const auto p = conway::mpi::partition_rows(
                    static_cast<std::size_t>(height), size, r);
                metrics << r << ',' << p.firstRow << ',' << p.rowCount << ','
                        << rank_alive[static_cast<size_t>(r)] << ','
                        << std::fixed << std::setprecision(6)
                        << rank_seconds[static_cast<size_t>(r)] << '\n';
            }
        }
        if (!o.board_path.empty()) {
            std::ofstream board_out(o.board_path);
            board_out << height << ' ' << width << '\n';
            for (int r = 0; r < height; ++r) {
                for (int c = 0; c < width; ++c) {
                    board_out << (board[static_cast<size_t>(r) * width + c] ? '1' : '0');
                }
                board_out << '\n';
            }
        }
        std::printf("%s conway-cluster pattern=%s P=%d steps=%d global_alive=%zu\n",
                    ok ? "PASS" : "FAIL", o.pattern.c_str(), size, o.steps,
                    alive);
        std::fflush(stdout);
    }
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm comm = MPI_COMM_NULL;
    try {
        const Options o = parse_options(argc, argv);
        comm = o.server ? server_comm(o) : client_comm(o);
        const int rc = check_conway(o, comm);
        MPI_Comm_free(&comm);
        MPI_Finalize();
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "conway_cluster: %s\n", e.what());
        if (comm != MPI_COMM_NULL) MPI_Abort(comm, 2);
        MPI_Abort(MPI_COMM_SELF, 2);
        return 2;
    }
}

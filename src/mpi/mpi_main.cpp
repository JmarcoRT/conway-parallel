// Ejecutable MPI del Juego de la Vida: evolucion distribuida con halos.
//
// Flujo:
//   1. rank 0 carga (o genera) el tablero y difunde dimensiones (MPI_Bcast);
//   2. reparto por franjas con MPI_Scatterv (mpi::scatter_board);
//   3. cada rank avanza su GameState local con core::step_mpi, que intercambia
//      halos con sus vecinos (mpi::halo_neighbors) antes de cada generacion;
//   4. al final se reensambla con MPI_Gatherv (mpi::gather_board) y se suman
//      las vivas con MPI_Allreduce;
//   5. opcionalmente rank 0 compara con el motor secuencial (--verify) y
//      escribe el tablero final en el formato de tests/dump_board.cpp (--dump).
//
// Uso:
//   mpirun -np P ./conway_mpi [archivo.txt | --generate ALTO ANCHO DENSIDAD SEMILLA]
//                             [--generations N] [--mode blocking|overlap]
//                             [--verify] [--dump RUTA|-] [--metadata RUTA|-]
//                             [--regions]
// --regions imprime el mapa fila -> rank con las filas que viajan a cada halo.
// --metadata escribe regiones, halos y metricas por rank en el CSV de
// mpi/region_metadata.hpp, que la UI lee sin MPI (read_decomposition_csv).
// --mode elige la estrategia de intercambio de halos (core::MpiHaloMode);
// por defecto blocking. Ambas deben producir el mismo tablero.
// DENSIDAD usa las unidades de input::GenerationConfig (1/10000, 0..10000).
// Codigo de salida: 0 OK, 1 error de entrada o verificacion fallida, 2 uso.

#include <mpi.h>

#include "core/engines/mpi.hpp"
#include "core/engines/sequential.hpp"
#include "core/game_state.hpp"
#include "input/generated_input.hpp"
#include "input/input.hpp"
#include "mpi/distribution.hpp"
#include "mpi/halo.hpp"
#include "mpi/local_state.hpp"
#include "mpi/partition.hpp"
#include "mpi/region_metadata.hpp"
#include "ui/mpi_regions_view.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

static_assert(conway::mpi::kNoNeighbor == conway::core::kNoHaloNeighbor,
              "el modulo mpi y el motor deben usar el mismo valor 'sin vecino'");

namespace {

struct Options {
    std::string path{"patterns/glider.txt"};
    bool generate{false};
    conway::input::GenerationConfig gen{0, 0, 0, 0};
    unsigned long long generations{10};
    conway::core::MpiHaloMode mode{conway::core::MpiHaloMode::Blocking};
    bool verify{false};
    std::string dumpPath;
    std::string metadataPath;
    bool showRegions{false};
};

bool parse_ull(const char* text, unsigned long long& out) {
    if (text == nullptr || *text == '\0' || *text == '-') {
        return false;
    }
    char* end = nullptr;
    out = std::strtoull(text, &end, 10);
    return *end == '\0';
}

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        unsigned long long v[4] = {0, 0, 0, 0};
        if (a == "--verify") {
            opt.verify = true;
        } else if (a == "--regions") {
            opt.showRegions = true;
        } else if (a == "--generations" && i + 1 < argc) {
            if (!parse_ull(argv[++i], opt.generations)) return false;
        } else if (a == "--mode" && i + 1 < argc) {
            const std::string m = argv[++i];
            if (m == "blocking") {
                opt.mode = conway::core::MpiHaloMode::Blocking;
            } else if (m == "overlap") {
                opt.mode = conway::core::MpiHaloMode::Overlap;
            } else {
                return false;
            }
        } else if (a == "--dump" && i + 1 < argc) {
            opt.dumpPath = argv[++i];
        } else if (a == "--metadata" && i + 1 < argc) {
            opt.metadataPath = argv[++i];
        } else if (a == "--generate" && i + 4 < argc) {
            for (int k = 0; k < 4; ++k) {
                if (!parse_ull(argv[i + 1 + k], v[k])) return false;
            }
            if (v[2] > 10000 || v[3] > 0xFFFFFFFFull) return false;
            opt.generate = true;
            opt.gen = {static_cast<std::size_t>(v[1]), static_cast<std::size_t>(v[0]),
                       static_cast<int>(v[2]), static_cast<std::uint32_t>(v[3])};
            i += 4;
        } else if (!a.empty() && a[0] != '-') {
            opt.path = a;
        } else {
            return false;
        }
    }
    return true;
}

// Mismo contrato que tests/dump_board.cpp (lo lee tests/compare_boards.py).
void write_dump(std::ostream& out, std::size_t width, std::size_t height,
                std::size_t generation, std::size_t alive,
                const std::vector<std::uint8_t>& board) {
    out << width << ' ' << height << ' ' << generation << ' ' << alive << '\n';
    for (std::size_t r = 0; r < height; ++r) {
        for (std::size_t c = 0; c < width; ++c) {
            out << (board[r * width + c] != 0 ? '1' : '0');
        }
        out << '\n';
    }
}

int run(int rank, int size, const Options& opt) {
    using namespace conway;
    const MPI_Comm comm = MPI_COMM_WORLD;

    // 1) Entrada en rank 0. dims = {fallo, alto, ancho}.
    InputData input{};
    std::string inputLabel = opt.generate ? "generada" : opt.path;
    unsigned long long dims[3] = {0, 0, 0};
    if (rank == 0) {
        try {
            input = opt.generate ? input::generateInput(opt.gen).input : loadInput(opt.path);
            mpi::require_int_counts(input.width, input.height);
            dims[1] = input.height;
            dims[2] = input.width;
        } catch (const std::exception& e) {
            std::cerr << "Error de entrada: " << e.what() << '\n';
            dims[0] = 1;
        }
    }
    MPI_Bcast(dims, 3, MPI_UNSIGNED_LONG_LONG, 0, comm);
    if (dims[0] != 0) {
        return 1;
    }
    const std::size_t H = static_cast<std::size_t>(dims[1]);
    const std::size_t W = static_cast<std::size_t>(dims[2]);

    // 2) Reparto y vecinos de halo.
    const auto part = mpi::partition_rows(H, size, rank);
    const auto neighbors = mpi::halo_neighbors(part);
    core::GameState local = mpi::scatter_board(input, W, H, 0, comm);
    const core::MpiStepContext ctx{comm, neighbors.up, neighbors.down, opt.mode};

    // 3) Evolucion. Barrier para alinear el inicio del cronometro.
    double haloSum = 0.0, computeSum = 0.0;
    unsigned long long bytesSum = 0, messagesSum = 0;
    MPI_Barrier(comm);
    const double t0 = MPI_Wtime();
    for (unsigned long long g = 0; g < opt.generations; ++g) {
        const auto st = core::step_mpi(local, ctx);
        haloSum += st.haloSeconds;
        computeSum += st.computeSeconds;
        bytesSum += st.bytesSent;
        messagesSum += st.messagesSent;
    }
    const double elapsed = MPI_Wtime() - t0;
    double elapsedMax = 0.0;
    MPI_Reduce(&elapsed, &elapsedMax, 1, MPI_DOUBLE, MPI_MAX, 0, comm);

    // 4) Reensamblado y resumen por rank.
    const std::size_t globalAlive = core::mpi_global_alive(local, comm);
    const std::vector<std::uint8_t> board = mpi::gather_board(local, H, 0, comm);

    unsigned long long info[5] = {part.firstRow, part.rowCount, local.aliveCount,
                                  bytesSum, messagesSum};
    int nb[2] = {neighbors.up, neighbors.down};
    double times[2] = {haloSum, computeSum};
    const std::size_t n = rank == 0 ? static_cast<std::size_t>(size) : 0;
    std::vector<unsigned long long> allInfo(5 * n);
    std::vector<int> allNb(2 * n);
    std::vector<double> allTimes(2 * n);
    MPI_Gather(info, 5, MPI_UNSIGNED_LONG_LONG, allInfo.data(), 5, MPI_UNSIGNED_LONG_LONG, 0, comm);
    MPI_Gather(nb, 2, MPI_INT, allNb.data(), 2, MPI_INT, 0, comm);
    MPI_Gather(times, 2, MPI_DOUBLE, allTimes.data(), 2, MPI_DOUBLE, 0, comm);

    if (rank != 0) {
        return 0;
    }

    char lib[MPI_MAX_LIBRARY_VERSION_STRING] = {};
    int libLen = 0;
    MPI_Get_library_version(lib, &libLen);
    const std::string libStr(lib);
    std::cout << "== Motor MPI: evolucion con intercambio de halos ==\n"
              << "MPI: " << libStr.substr(0, libStr.find('\n')) << '\n'
              << "Procesos: " << size << " | Tablero: " << H << " filas x " << W
              << " columnas | Entrada: " << inputLabel
              << " | Generaciones: " << opt.generations << '\n'
              << "Intercambio de halos: "
              << (opt.mode == core::MpiHaloMode::Overlap
                      ? "overlap (Isend/Irecv + computo interior + Waitall)"
                      : "blocking (MPI_Sendrecv)")
              << "\n\n";

    // Instantanea de regiones/halos: la misma que se exporta con --metadata y
    // la que consume la UI. Las metricas son las observadas en cada rank.
    mpi::DecompositionSnapshot snap;
    snap.layout = mpi::describe_decomposition(W, H, size);
    snap.generation = local.generation;
    snap.haloMode = opt.mode == core::MpiHaloMode::Overlap ? "overlap" : "blocking";
    bool regionsMatch = true;
    for (int r = 0; r < size; ++r) {
        const auto k = static_cast<std::size_t>(r);
        const auto& reg = snap.layout.regions[k];
        regionsMatch = regionsMatch && reg.firstRow == allInfo[5 * k] &&
                       reg.rowCount == allInfo[5 * k + 1] &&
                       reg.top.sourceRank == allNb[2 * k] &&
                       reg.bottom.sourceRank == allNb[2 * k + 1];
        snap.metrics.push_back({r, static_cast<std::size_t>(allInfo[5 * k + 2]),
                                allTimes[2 * k], allTimes[2 * k + 1],
                                static_cast<std::size_t>(allInfo[5 * k + 4]),
                                static_cast<std::size_t>(allInfo[5 * k + 3])});
    }
    for (const auto& line : ui::mpiRegionTableLines(snap)) {
        std::cout << line << '\n';
    }
    if (opt.showRegions) {
        std::cout << '\n';
        for (const auto& line : ui::mpiRowMapLines(snap.layout)) {
            std::cout << line << '\n';
        }
    }
    std::cout << "\nVivas finales (MPI_Allreduce): " << globalAlive << '\n'
              << "Tiempo del lote de generaciones (max entre ranks): " << std::fixed
              << std::setprecision(6) << elapsedMax << " s\n";

    if (!opt.dumpPath.empty()) {
        if (opt.dumpPath == "-") {
            write_dump(std::cout, W, H, local.generation, globalAlive, board);
        } else {
            std::ofstream out(opt.dumpPath);
            write_dump(out, W, H, local.generation, globalAlive, board);
            if (!out) {
                std::cerr << "No se pudo escribir " << opt.dumpPath << '\n';
                return 1;
            }
        }
    }

    if (!opt.metadataPath.empty()) {
        if (opt.metadataPath == "-") {
            mpi::write_decomposition_csv(std::cout, snap);
        } else {
            std::ofstream out(opt.metadataPath);
            mpi::write_decomposition_csv(out, snap);
            out.flush();
            if (!out) {
                std::cerr << "No se pudo escribir " << opt.metadataPath << '\n';
                return 1;
            }
        }
    }

    if (!opt.verify) {
        return 0;
    }

    // 5) Oraculo: motor secuencial sobre el tablero completo (fuera del cronometro).
    core::GameState reference = mpi::make_local_state(input, mpi::partition_rows(H, 1, 0));
    for (unsigned long long g = 0; g < opt.generations; ++g) {
        core::step_sequential(reference);
    }
    const bool sameBoard = board == mpi::pack_local_rows(reference);
    const bool sameAlive = globalAlive == reference.aliveCount;
    const bool sameGen = local.generation == reference.generation;
    std::cout << "\nVerificacion contra motor secuencial:\n"
              << (sameBoard ? "[OK]    " : "[FALLO] ") << "tablero final identico celula a celula\n"
              << (sameAlive ? "[OK]    " : "[FALLO] ") << "vivas: MPI " << globalAlive
              << " / secuencial " << reference.aliveCount << '\n'
              << (sameGen ? "[OK]    " : "[FALLO] ") << "generacion: " << local.generation << '\n'
              << (regionsMatch ? "[OK]    " : "[FALLO] ")
              << "regiones y vecinos usados por los ranks == metadatos para la UI\n";
    const bool ok = sameBoard && sameAlive && sameGen && regionsMatch;
    std::cout << "\nRESULTADO: " << (ok ? "OK" : "FALLO") << '\n';
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    Options opt;
    int status = 0;
    if (!parse_args(argc, argv, opt)) {
        if (rank == 0) {
            std::cerr << "Uso: mpirun -np P " << argv[0]
                      << " [archivo.txt | --generate ALTO ANCHO DENSIDAD SEMILLA]"
                         " [--generations N] [--mode blocking|overlap] [--verify]"
                         " [--dump RUTA|-] [--metadata RUTA|-] [--regions]\n";
        }
        status = 2;
    } else {
        try {
            status = run(rank, size, opt);
        } catch (const std::exception& e) {
            std::cerr << "rank " << rank << ": " << e.what() << '\n';
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    MPI_Bcast(&status, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Finalize();
    return status;
}

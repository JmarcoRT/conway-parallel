// Prototipo MPI aislado (Semana 6): procesos + reparto horizontal de filas.
//
// Sin --steps comprueba solamente la distribucion. Con --steps N evoluciona
// con halos y compara cada paso contra el motor secuencial. Comprueba que:
//   1. MPI se inicializa/finaliza bien y cada proceso conoce su rank y size;
//   2. el reparto de filas es reproducible, contiguo y balanceado;
//   3. no se pierden ni se duplican filas (el tablero reensamblado a partir
//      de los GameState locales es identico a la entrada);
//   4. cada particion se corresponde con un core::GameState local valido.
//
// Flujo (el del documento S4): rank 0 lee la entrada y difunde las dimensiones
// (MPI_Bcast); reparte las franjas con MPI_Scatterv (con "v" porque el reparto
// puede ser desigual); cada rank arma su GameState local; al final rank 0
// reune las franjas con MPI_Gatherv para verificar el reensamblado.
//
// Uso: mpirun -np P ./mpi_partition_prototype [archivo.txt | --synthetic ALTO ANCHO] [--show]

#include <mpi.h>
#include <unistd.h>

#include "core/game_state.hpp"
#include "core/engines/mpi.hpp"
#include "core/engines/sequential.hpp"
#include "input/input.hpp"
#include "mpi/local_state.hpp"
#include "mpi/partition.hpp"
#include "mpi/counts.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string path{"patterns/glider.txt"};
    bool synthetic{false};
    std::size_t synHeight{0};
    std::size_t synWidth{0};
    bool show{false};
    int steps{0};
};

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--show") {
            opt.show = true;
        } else if (a == "--steps" && i + 1 < argc) {
            const std::string value = argv[++i];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), opt.steps);
            if (error != std::errc{} || end != value.data() + value.size() || opt.steps < 0)
                return false;
        } else if (a == "--synthetic" && i + 2 < argc) {
            char* e1 = nullptr;
            char* e2 = nullptr;
            const unsigned long h = std::strtoul(argv[i + 1], &e1, 10);
            const unsigned long w = std::strtoul(argv[i + 2], &e2, 10);
            if (*e1 != '\0' || *e2 != '\0' || h == 0 || w == 0 ||
                h > 100000 || w > 100000) {
                return false;
            }
            opt.synthetic = true;
            opt.synHeight = h;
            opt.synWidth = w;
            i += 2;
        } else if (!a.empty() && a[0] != '-') {
            opt.path = a;
        } else {
            return false;
        }
    }
    return true;
}

// Tablero determinista donde (casi) cada fila es distinta: sirve para detectar
// filas perdidas, duplicadas o desplazadas al reensamblar.
InputData make_synthetic(std::size_t h, std::size_t w) {
    InputData in{};
    in.width = w;
    in.height = h;
    in.cells.assign(conway::mpi::checked_cell_count(w, h), 0);
    for (std::size_t r = 0; r < h; ++r) {
        for (std::size_t c = 0; c < w; ++c) {
            in.cells[r * w + c] = ((r * 7 + c * 3 + r * c) % 5 == 0) ? 1 : 0;
        }
    }
    return in;
}

// Invariantes del GameState local (forma comun del core, ver ARCHITECTURE.md).
bool local_state_ok(const conway::core::GameState& s,
                    const conway::mpi::RowPartition& p,
                    std::size_t globalWidth) {
    if (s.width != globalWidth || s.height != p.rowCount ||
        s.stride != globalWidth + 2 || s.generation != 0) {
        return false;
    }
    const std::size_t expected = s.stride * (s.height + 2);
    if (s.current.size() != expected || s.next.size() != expected) {
        return false;
    }
    // Halos (filas 0 y height+1, columnas 0 y width+1) a 0 en esta etapa.
    for (std::size_t r = 0; r < s.height + 2; ++r) {
        const bool haloRow = (r == 0 || r == s.height + 1);
        for (std::size_t c = 0; c < s.stride; ++c) {
            const bool haloCol = (c == 0 || c == s.width + 1);
            if ((haloRow || haloCol) && s.current[r * s.stride + c] != 0) {
                return false;
            }
        }
    }
    return true;
}

void check(bool ok, const std::string& label, bool& all) {
    std::cout << (ok ? "[OK]    " : "[FALLO] ") << label << '\n';
    all = all && ok;
}

int run(int rank, int size, const Options& opt) {
    // 1) Entrada: solo rank 0 lee (o genera) el tablero y difunde las
    //    dimensiones. dims = {fallo, alto, ancho}.
    InputData input{};  // solo es valida en rank 0
    unsigned long long dims[3] = {0, 0, 0};
    if (rank == 0) {
        try {
            input = opt.synthetic ? make_synthetic(opt.synHeight, opt.synWidth)
                                  : loadInput(opt.path);
            // Los conteos de MPI son int: el tablero completo debe caber.
            (void)conway::mpi::checked_cell_count(input.width, input.height);
            dims[1] = input.height;
            dims[2] = input.width;
        } catch (const std::exception& e) {
            std::cerr << "Error de entrada: " << e.what() << '\n';
            dims[0] = 1;
        }
    }
    MPI_Bcast(dims, 3, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
    if (dims[0] != 0) {
        return 1;  // todos los ranks salen limpios, ninguno queda en una colectiva
    }
    const std::size_t H = static_cast<std::size_t>(dims[1]);
    const std::size_t W = static_cast<std::size_t>(dims[2]);

    // 2) Particion + reparto con Scatterv + estado local sobre el GameState comun.
    const auto part = conway::mpi::partition_rows(H, size, rank);

    std::vector<int> scCounts, scDispls;
    if (rank == 0) {
        scCounts.resize(static_cast<std::size_t>(size));
        scDispls.resize(static_cast<std::size_t>(size));
        for (int r = 0; r < size; ++r) {
            const auto pr = conway::mpi::partition_rows(H, size, r);
            scCounts[static_cast<std::size_t>(r)] = static_cast<int>(pr.rowCount * W);
            scDispls[static_cast<std::size_t>(r)] = static_cast<int>(pr.firstRow * W);
        }
    }
    std::vector<std::uint8_t> myRows(part.rowCount * W);
    MPI_Scatterv(input.cells.data(), scCounts.data(), scDispls.data(),
                 MPI_UNSIGNED_CHAR, myRows.data(), static_cast<int>(myRows.size()),
                 MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    auto state = conway::mpi::make_local_state_from_rows(W, part, myRows);
    const int localOk = local_state_ok(state, part, W) ? 1 : 0;
    int allLocalOk = 0;
    MPI_Allreduce(&localOk, &allLocalOk, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    // 3) Cada proceso informa de si mismo: rank que cree tener, pid, filas, vivas.
    //    info = {rank, pid, firstRow, rowCount, alive}
    unsigned long long info[5] = {
        static_cast<unsigned long long>(rank),
        static_cast<unsigned long long>(::getpid()),
        static_cast<unsigned long long>(part.firstRow),
        static_cast<unsigned long long>(part.rowCount),
        static_cast<unsigned long long>(state.aliveCount)};
    std::vector<unsigned long long> all(rank == 0 ? 5 * static_cast<std::size_t>(size) : 0);
    MPI_Gather(info, 5, MPI_UNSIGNED_LONG_LONG, all.data(), 5,
               MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);

    // 4) Reensamblado: Gatherv de las filas propias (sin halo) hacia rank 0.
    const std::vector<std::uint8_t> mine = conway::mpi::pack_local_rows(state);
    std::vector<int> counts, displs;
    std::vector<std::uint8_t> board;
    if (rank == 0) {
        counts.resize(static_cast<std::size_t>(size));
        displs.resize(static_cast<std::size_t>(size));
        board.assign(W * H, 0xFF);  // 0xFF = "sin escribir"
        for (int r = 0; r < size; ++r) {
            const auto first = all[5 * static_cast<std::size_t>(r) + 2];
            const auto cnt = all[5 * static_cast<std::size_t>(r) + 3];
            counts[static_cast<std::size_t>(r)] = static_cast<int>(cnt * W);
            displs[static_cast<std::size_t>(r)] = static_cast<int>(first * W);
        }
    }
    MPI_Gatherv(mine.data(), static_cast<int>(mine.size()), MPI_UNSIGNED_CHAR,
                board.data(), counts.data(), displs.data(), MPI_UNSIGNED_CHAR,
                0, MPI_COMM_WORLD);

    unsigned long long aliveLocal = state.aliveCount, aliveTotal = 0;
    MPI_Reduce(&aliveLocal, &aliveTotal, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0,
               MPI_COMM_WORLD);

    // 5) Informe y verificacion en rank 0.
    int status = 0;
    if (rank == 0) {
        const auto field = [&](int r, std::size_t k) {
            return all[5 * static_cast<std::size_t>(r) + k];
        };

        std::cout << "== Prototipo MPI: reparto horizontal de filas ==\n";
        char lib[MPI_MAX_LIBRARY_VERSION_STRING] = {};
        int libLen = 0;
        MPI_Get_library_version(lib, &libLen);
        const std::string libStr(lib);
        std::cout << "MPI: " << libStr.substr(0, libStr.find('\n')) << '\n';
        std::cout << "Distribucion: rank 0 lee y reparte con MPI_Scatterv; reune con MPI_Gatherv\n";
        std::cout << "Procesos: " << size << " | Tablero: " << H << " filas x "
                  << W << " columnas | Entrada: "
                  << (opt.synthetic ? std::string("sintetica") : opt.path)
                  << "\n\n";

        std::cout << "rank     pid  filas globales  n_filas  vivas  stride  buffer(bytes)\n";
        for (int r = 0; r < size; ++r) {
            const std::size_t first = static_cast<std::size_t>(field(r, 2));
            const std::size_t cnt = static_cast<std::size_t>(field(r, 3));
            const std::string range =
                cnt == 0 ? std::string("(vacia)")
                         : "[" + std::to_string(first) + ".." +
                               std::to_string(first + cnt - 1) + "]";
            std::cout << std::setw(4) << r << std::setw(8) << field(r, 1)
                      << std::setw(16) << range << std::setw(9) << cnt
                      << std::setw(7) << field(r, 4) << std::setw(8) << (W + 2)
                      << std::setw(15) << (W + 2) * (cnt + 2) << '\n';
        }

        if (opt.show || H <= 16) {
            std::cout << "\nFilas recibidas por cada rank (indice global: contenido):\n";
            for (int r = 0; r < size; ++r) {
                const std::size_t first = static_cast<std::size_t>(field(r, 2));
                const std::size_t cnt = static_cast<std::size_t>(field(r, 3));
                std::cout << "rank " << r << (cnt == 0 ? ": (sin filas)\n" : ":\n");
                for (std::size_t i = 0; i < cnt; ++i) {
                    std::cout << "  fila " << std::setw(3) << (first + i) << ": ";
                    for (std::size_t c = 0; c < W; ++c) {
                        std::cout << (board[(first + i) * W + c] == 1 ? '1' : '0');
                    }
                    std::cout << '\n';
                }
            }
        }

        std::cout << "\nVerificaciones:\n";
        bool ok = true;

        bool identified = true;
        std::set<unsigned long long> pids;
        for (int r = 0; r < size; ++r) {
            identified = identified && field(r, 0) == static_cast<unsigned long long>(r);
            pids.insert(field(r, 1));
        }
        check(identified, "cada proceso reporta su propio rank (0.." +
                              std::to_string(size - 1) + ") de " +
                              std::to_string(size),
              ok);
        check(pids.size() == static_cast<std::size_t>(size),
              "los " + std::to_string(size) + " procesos son procesos del SO distintos (pids unicos)",
              ok);

        bool contiguous = true, sameAsPlan = true, balanced = true, nonIncreasing = true;
        std::size_t expectedStart = 0, total = 0, minCnt = H, maxCnt = 0, prevCnt = H;
        for (int r = 0; r < size; ++r) {
            const std::size_t first = static_cast<std::size_t>(field(r, 2));
            const std::size_t cnt = static_cast<std::size_t>(field(r, 3));
            contiguous = contiguous && first == expectedStart;
            expectedStart = first + cnt;
            total += cnt;
            minCnt = std::min(minCnt, cnt);
            maxCnt = std::max(maxCnt, cnt);
            nonIncreasing = nonIncreasing && cnt <= prevCnt;
            prevCnt = cnt;
            const auto plan = conway::mpi::partition_rows(H, size, r);
            sameAsPlan = sameAsPlan && plan.firstRow == first && plan.rowCount == cnt;
        }
        balanced = (maxCnt - minCnt) <= 1;
        check(contiguous, "bloques contiguos en orden de rank (sin huecos ni solapes)", ok);
        check(total == H, "suma de filas repartidas = alto global (" +
                              std::to_string(total) + " de " + std::to_string(H) + ")",
              ok);
        check(balanced && nonIncreasing,
              "reparto balanceado: max-min <= 1 y las filas sobrantes van a los primeros ranks", ok);
        check(sameAsPlan, "lo reportado por cada rank coincide con partition_rows(alto, size, rank)", ok);
        check(board == input.cells,
              "tablero reensamblado desde los GameState locales == entrada (ninguna fila perdida/duplicada)",
              ok);

        unsigned long long inputAlive = 0;
        for (const std::uint8_t v : input.cells) {
            inputAlive += (v != 0) ? 1 : 0;
        }
        check(aliveTotal == inputAlive,
              "celulas vivas: suma de ranks (" + std::to_string(aliveTotal) +
                  ") == entrada (" + std::to_string(inputAlive) + ")",
              ok);
        check(allLocalOk != 0,
              "GameState local de cada rank valido (width, height local, stride=width+2, buffers, halos a 0)",
              ok);

        std::cout << "\nRESULTADO: " << (ok ? "OK" : "FALLO") << '\n';
        status = ok ? 0 : 1;
    }
    // Keep the original distribution demo intact; evolution is opt-in.
    std::optional<conway::core::GameState> reference;
    if (rank == 0 && opt.steps > 0)
        reference = conway::mpi::make_local_state(input, conway::mpi::partition_rows(H, 1, 0));
    for (int step = 0; step < opt.steps; ++step) {
        const auto globalAlive = conway::core::step_mpi(state, part, MPI_COMM_WORLD);
        const auto rows = conway::mpi::pack_local_rows(state);
        MPI_Gatherv(rows.data(), static_cast<int>(rows.size()), MPI_UINT8_T,
                    board.data(), counts.data(), displs.data(), MPI_UINT8_T, 0, MPI_COMM_WORLD);
        int generationOk = state.generation == static_cast<std::size_t>(step) + 1;
        int allGenerationsOk = 0;
        MPI_Allreduce(&generationOk, &allGenerationsOk, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        if (rank == 0) {
            conway::core::step_sequential(*reference);
            const bool equivalent = allGenerationsOk &&
                state.generation == reference->generation &&
                globalAlive == reference->aliveCount &&
                board == conway::mpi::pack_local_rows(*reference);
            std::cout << (equivalent ? "[OK] " : "[FALLO] ") << "generacion "
                      << state.generation << ": celulas, generacion y poblacion\n";
            if (!equivalent) status = 1;
        }
    }
    if (rank == 0 && opt.steps > 0)
        std::cout << "EVOLUCION: " << (status == 0 ? "OK" : "FALLO") << '\n';
    return status;
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
                      << " [archivo.txt | --synthetic ALTO ANCHO] [--show] [--steps N]\n";
        }
        status = 2;
    } else {
        try {
            status = run(rank, size, opt);
        } catch (const std::exception& error) {
            std::cerr << "rank " << rank << ": " << error.what() << '\n';
            MPI_Abort(MPI_COMM_WORLD, 2);
            return 2;
        }
    }
    MPI_Bcast(&status, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Finalize();
    return status;
}

// Pruebas de vecinos de halo y de la descomposicion con halos (sin MPI).
//
// La segunda parte emula en un solo proceso lo que hace el motor MPI en cada
// generacion: copia la fila propia del vecino en el halo (lo mismo que
// transportan los MPI_Sendrecv/MPI_Isend) y avanza cada GameState local con el
// recorrido secuencial existente. El resultado reensamblado debe coincidir,
// generacion a generacion, con el motor secuencial sobre el tablero completo.
// Asi se valida el contrato de halos sin depender de mpirun.
#include "core/engines/sequential.hpp"
#include "core/game_state.hpp"
#include "input/input.hpp"
#include "mpi/halo.hpp"
#include "mpi/local_state.hpp"
#include "mpi/partition.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "FALLO: " << what << '\n';
        ++failures;
    }
}

InputData synthetic(std::size_t h, std::size_t w) {
    InputData in{};
    in.width = w;
    in.height = h;
    in.cells.assign(w * h, 0);
    for (std::size_t r = 0; r < h; ++r) {
        for (std::size_t c = 0; c < w; ++c) {
            in.cells[r * w + c] = ((r * 7 + c * 3 + r * c) % 5 == 0) ? 1 : 0;
        }
    }
    return in;
}

int owner_of(std::size_t globalRow, std::size_t h, int p) {
    for (int r = 0; r < p; ++r) {
        const auto part = conway::mpi::partition_rows(h, p, r);
        if (globalRow >= part.firstRow && globalRow < part.endRow()) {
            return r;
        }
    }
    return conway::mpi::kNoNeighbor;
}

// Emula el intercambio: halo superior <- ultima fila propia de `up`,
// halo inferior <- primera fila propia de `down`. Solo columnas logicas.
void emulate_exchange(std::vector<conway::core::GameState>& locals,
                      const std::vector<conway::mpi::HaloNeighbors>& nb) {
    for (std::size_t r = 0; r < locals.size(); ++r) {
        auto& s = locals[r];
        if (s.height == 0) {
            continue;
        }
        if (nb[r].hasUp()) {
            const auto& a = locals[static_cast<std::size_t>(nb[r].up)];
            std::copy_n(a.current.data() + a.height * a.stride + 1, s.width,
                        s.current.data() + 1);
        }
        if (nb[r].hasDown()) {
            const auto& b = locals[static_cast<std::size_t>(nb[r].down)];
            std::copy_n(b.current.data() + 1 * b.stride + 1, s.width,
                        s.current.data() + (s.height + 1) * s.stride + 1);
        }
    }
}

bool column_halos_zero(const conway::core::GameState& s) {
    for (std::size_t r = 0; r < s.height + 2; ++r) {
        if (s.current[r * s.stride] != 0 || s.current[r * s.stride + s.width + 1] != 0) {
            return false;
        }
    }
    return true;
}

void check_decomposition(const InputData& input, int p, int generations,
                         const std::string& label) {
    using namespace conway;
    const auto global = mpi::partition_rows(input.height, 1, 0);
    core::GameState reference = mpi::make_local_state(input, global);

    std::vector<core::GameState> locals;
    std::vector<mpi::HaloNeighbors> nb;
    for (int r = 0; r < p; ++r) {
        const auto part = mpi::partition_rows(input.height, p, r);
        locals.push_back(mpi::make_local_state(input, part));
        nb.push_back(mpi::halo_neighbors(part));
    }

    for (int g = 1; g <= generations; ++g) {
        emulate_exchange(locals, nb);
        for (auto& s : locals) {
            core::step_sequential(s);
        }
        core::step_sequential(reference);

        std::vector<std::uint8_t> board;
        std::size_t alive = 0;
        bool sameGeneration = true;
        bool globalEdgesZero = true;
        for (int r = 0; r < p; ++r) {
            const auto& s = locals[static_cast<std::size_t>(r)];
            const auto rows = mpi::pack_local_rows(s);
            board.insert(board.end(), rows.begin(), rows.end());
            alive += s.aliveCount;
            sameGeneration = sameGeneration && s.generation == reference.generation;
            globalEdgesZero = globalEdgesZero && column_halos_zero(s);
            // Un halo sin vecino (borde global) nunca se escribe: debe seguir a 0.
            if (s.height > 0 && !nb[static_cast<std::size_t>(r)].hasUp()) {
                globalEdgesZero = globalEdgesZero &&
                    std::all_of(s.current.begin(), s.current.begin() + static_cast<std::ptrdiff_t>(s.stride),
                                [](std::uint8_t v) { return v == 0; });
            }
            if (s.height > 0 && !nb[static_cast<std::size_t>(r)].hasDown()) {
                const auto first = s.current.begin() +
                    static_cast<std::ptrdiff_t>((s.height + 1) * s.stride);
                globalEdgesZero = globalEdgesZero &&
                    std::all_of(first, first + static_cast<std::ptrdiff_t>(s.stride),
                                [](std::uint8_t v) { return v == 0; });
            }
        }
        const std::string where = label + " p=" + std::to_string(p) +
                                  " gen=" + std::to_string(g);
        expect(board == mpi::pack_local_rows(reference), "tablero reensamblado == secuencial: " + where);
        expect(alive == reference.aliveCount, "suma de vivas locales == secuencial: " + where);
        expect(sameGeneration, "generacion local == secuencial: " + where);
        expect(globalEdgesZero, "bordes globales y columnas halo a 0: " + where);
        if (failures > 20) {
            return;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace conway::mpi;
    const std::string root = argc > 1 ? argv[1] : ".";

    // 1) Vecinos: coinciden con el dueno de la fila adyacente, son simetricos
    //    y los ranks vacios no participan.
    for (std::size_t h = 1; h <= 40; ++h) {
        for (int p = 1; p <= 12; ++p) {
            for (int r = 0; r < p; ++r) {
                const auto part = partition_rows(h, p, r);
                const auto n = halo_neighbors(part);
                const std::string where = "h=" + std::to_string(h) + " p=" +
                                          std::to_string(p) + " r=" + std::to_string(r);
                if (part.empty()) {
                    expect(!n.hasUp() && !n.hasDown(), "rank vacio sin vecinos: " + where);
                    continue;
                }
                const int expectedUp = part.firstRow == 0 ? kNoNeighbor
                                                          : owner_of(part.firstRow - 1, h, p);
                const int expectedDown = part.endRow() == h ? kNoNeighbor
                                                            : owner_of(part.endRow(), h, p);
                expect(n.up == expectedUp, "vecino superior = dueno de firstRow-1: " + where);
                expect(n.down == expectedDown, "vecino inferior = dueno de endRow: " + where);
                if (n.hasDown()) {
                    expect(halo_neighbors(partition_rows(h, p, n.down)).up == r,
                           "simetria down/up: " + where);
                }
                if (n.hasUp()) {
                    expect(n.up == r - 1, "vecino superior es el rank anterior: " + where);
                }
            }
        }
    }

    // 2) Ejemplos concretos.
    {
        const auto n0 = halo_neighbors(partition_rows(10, 4, 0));
        const auto n3 = halo_neighbors(partition_rows(10, 4, 3));
        expect(n0.up == kNoNeighbor && n0.down == 1, "10/4: rank0 sin arriba, abajo = 1");
        expect(n3.up == 2 && n3.down == kNoNeighbor, "10/4: rank3 arriba = 2, sin abajo");
        const auto n2 = halo_neighbors(partition_rows(3, 5, 2));  // 1,1,1,0,0
        expect(n2.up == 1 && n2.down == kNoNeighbor, "3/5: ultimo rank con filas sin abajo");
        const auto solo = halo_neighbors(partition_rows(7, 1, 0));
        expect(!solo.hasUp() && !solo.hasDown(), "1 proceso: sin vecinos");
    }

    // 3) Descomposicion + halos == secuencial, incluidos 1/2/4 procesos,
    //    reparto no divisible, ranks de 1 fila y ranks vacios.
    const std::vector<std::string> patterns = {
        "glider.txt", "blinker.txt", "beacon.txt", "toad.txt", "pulsar.txt",
        "gosper_glider_gun.txt"};
    for (const auto& name : patterns) {
        InputData in{};
        try {
            in = loadInput(root + "/patterns/" + name);
        } catch (const std::exception& e) {
            expect(false, "no se pudo cargar " + name + ": " + e.what());
            continue;
        }
        for (int p : {1, 2, 3, 4, 7, 8}) {
            check_decomposition(in, p, 30, name);
        }
    }
    for (int p : {1, 2, 4, 5, 13}) {
        check_decomposition(synthetic(11, 9), p, 25, "sintetico 11x9");
        check_decomposition(synthetic(4, 17), p, 10, "sintetico 4x17");
        check_decomposition(synthetic(64, 48), p, 20, "sintetico 64x48");
    }

    if (failures == 0) {
        std::cout << "mpi_halo_tests: OK\n";
        return 0;
    }
    std::cerr << "mpi_halo_tests: " << failures << " fallo(s)\n";
    return 1;
}

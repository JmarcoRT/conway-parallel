// Pruebas de la particion horizontal y del GameState local (sin MPI).
#include "core/game_state.hpp"
#include "input/input.hpp"
#include "mpi/local_state.hpp"
#include "mpi/partition.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

void expect(bool cond, const char* what) {
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

} // namespace

int main() {
    using namespace conway::mpi;

    // 1) Cobertura exacta, contigua y balanceada para muchas combinaciones,
    //    incluidas size > alto (particiones vacias).
    for (std::size_t h = 1; h <= 64; ++h) {
        for (int p = 1; p <= 80; ++p) {
            std::size_t next = 0;
            std::size_t minC = h, maxC = 0;
            bool contiguous = true, nonInc = true;
            std::size_t prev = h;
            for (int r = 0; r < p; ++r) {
                const auto part = partition_rows(h, p, r);
                contiguous = contiguous && part.firstRow == next &&
                             part.rank == r && part.size == p &&
                             part.globalHeight == h;
                next = part.endRow();
                minC = std::min(minC, part.rowCount);
                maxC = std::max(maxC, part.rowCount);
                nonInc = nonInc && part.rowCount <= prev;
                prev = part.rowCount;
                // Determinismo: misma llamada, mismo resultado.
                const auto again = partition_rows(h, p, r);
                contiguous = contiguous && again.firstRow == part.firstRow &&
                             again.rowCount == part.rowCount;
            }
            expect(contiguous, "bloques contiguos y deterministas");
            expect(next == h, "suma de filas == alto global");
            expect(maxC - minC <= 1, "balance max-min <= 1");
            expect(nonInc, "filas sobrantes en los primeros ranks");
        }
    }

    // 2) Ejemplos concretos (resto != 0).
    {
        const auto a = partition_rows(10, 4, 0);  // 3,3,2,2
        const auto b = partition_rows(10, 4, 1);
        const auto c = partition_rows(10, 4, 2);
        const auto d = partition_rows(10, 4, 3);
        expect(a.firstRow == 0 && a.rowCount == 3, "10 filas / 4: rank0 = [0,3)");
        expect(b.firstRow == 3 && b.rowCount == 3, "10 filas / 4: rank1 = [3,6)");
        expect(c.firstRow == 6 && c.rowCount == 2, "10 filas / 4: rank2 = [6,8)");
        expect(d.firstRow == 8 && d.rowCount == 2, "10 filas / 4: rank3 = [8,10)");
        const auto e = partition_rows(3, 5, 4);
        expect(e.empty() && e.firstRow == 3, "3 filas / 5: rank4 vacio");
    }

    // 3) Argumentos invalidos.
    auto throws = [](auto&& f) {
        try { f(); } catch (const std::invalid_argument&) { return true; } catch (...) {}
        return false;
    };
    expect(throws([] { partition_rows(5, 0, 0); }), "size 0 lanza");
    expect(throws([] { partition_rows(5, 3, 3); }), "rank == size lanza");
    expect(throws([] { partition_rows(5, 3, -1); }), "rank negativo lanza");

    // 4) GameState local: forma comun, contenido y reensamblado.
    for (std::size_t h : {1u, 5u, 6u, 13u}) {
        for (int p : {1, 2, 3, 4, 7, 16}) {
            const InputData in = synthetic(h, 9);
            std::vector<std::uint8_t> board;
            std::size_t alive = 0;
            for (int r = 0; r < p; ++r) {
                const auto part = partition_rows(h, p, r);
                const auto s = make_local_state(in, part);
                expect(s.width == 9 && s.height == part.rowCount &&
                           s.stride == 11 && s.generation == 0,
                       "forma del GameState local");
                expect(s.current.size() == s.stride * (s.height + 2) &&
                           s.next.size() == s.current.size(),
                       "tamano de buffers con halo");
                bool halo0 = true;
                for (std::size_t c = 0; c < s.stride; ++c) {
                    halo0 = halo0 && s.current[c] == 0 &&
                            s.current[(s.height + 1) * s.stride + c] == 0;
                }
                expect(halo0, "filas halo a 0");
                const auto rows = pack_local_rows(s);
                board.insert(board.end(), rows.begin(), rows.end());
                alive += s.aliveCount;
            }
            expect(board == in.cells, "reensamblado == entrada");
            expect(alive == static_cast<std::size_t>(
                                std::count(in.cells.begin(), in.cells.end(), std::uint8_t{1})),
                   "vivas locales suman el total");
        }
    }

    // 4b) Camino MPI: estado local desde filas ya recibidas (equivale a Scatterv).
    {
        const InputData in = synthetic(10, 7);
        for (int r = 0; r < 4; ++r) {
            const auto part = partition_rows(10, 4, r);
            std::vector<std::uint8_t> rows(
                in.cells.begin() + static_cast<std::ptrdiff_t>(part.firstRow * 7),
                in.cells.begin() + static_cast<std::ptrdiff_t>(part.endRow() * 7));
            const auto a = make_local_state_from_rows(7, part, rows);
            const auto b = make_local_state(in, part);
            expect(a.current == b.current && a.aliveCount == b.aliveCount &&
                       a.height == b.height && a.stride == b.stride,
                   "from_rows == make_local_state");
            rows.push_back(0);
            auto bad = [&] { make_local_state_from_rows(7, part, rows); };
            bool thrown = false;
            try { bad(); } catch (const std::invalid_argument&) { thrown = true; }
            expect(thrown, "filas de tamano erroneo lanza");
        }
    }

    // 5) Particion que no corresponde a la entrada.
    {
        const InputData in = synthetic(6, 4);
        const auto wrong = partition_rows(7, 2, 0);
        expect(throws([&] { make_local_state(in, wrong); }),
               "particion de otro alto lanza");
    }

    if (failures == 0) {
        std::cout << "partition_tests: OK\n";
        return 0;
    }
    std::cerr << "partition_tests: " << failures << " fallo(s)\n";
    return 1;
}

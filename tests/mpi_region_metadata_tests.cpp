// Pruebas de los metadatos de regiones/halos para la UI (sin MPI).
#include "mpi/halo.hpp"
#include "mpi/partition.hpp"
#include "mpi/region_metadata.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void expect(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "FALLO: " << what << '\n';
        ++failures;
    }
}

template <class F>
void expect_throws(F&& f, const std::string& what) {
    try {
        f();
        expect(false, "deberia lanzar: " + what);
    } catch (const std::exception&) {
    }
}

std::string replace_line(const std::string& text, std::size_t index, const std::string& line) {
    std::istringstream in(text);
    std::ostringstream out;
    std::string l;
    for (std::size_t i = 0; std::getline(in, l); ++i) {
        out << (i == index ? line : l) << '\n';
    }
    return out.str();
}

} // namespace

int main() {
    using namespace conway::mpi;

    // 1) Geometria coherente con partition_rows/halo_neighbors para muchos casos.
    for (std::size_t h = 1; h <= 30; ++h) {
        for (int p = 1; p <= 10; ++p) {
            const auto L = describe_decomposition(5, h, p);
            const std::string where = "h=" + std::to_string(h) + " p=" + std::to_string(p);
            expect(L.regions.size() == static_cast<std::size_t>(p), "una region por rank: " + where);
            std::size_t covered = 0, messages = 0, nonEmpty = 0;
            for (const auto& reg : L.regions) {
                const auto part = partition_rows(h, p, reg.rank);
                const auto nb = halo_neighbors(part);
                expect(reg.firstRow == part.firstRow && reg.rowCount == part.rowCount,
                       "region == particion: " + where);
                expect(reg.top.sourceRank == nb.up && reg.bottom.sourceRank == nb.down,
                       "halos == vecinos: " + where);
                if (!reg.top.isGlobalBorder()) {
                    expect(reg.top.sourceGlobalRow + 1 == reg.firstRow &&
                               owner_of_row(L, reg.top.sourceGlobalRow) == reg.top.sourceRank,
                           "halo superior = fila anterior de su dueno: " + where);
                }
                if (!reg.bottom.isGlobalBorder()) {
                    expect(reg.bottom.sourceGlobalRow == reg.endRow() &&
                               owner_of_row(L, reg.bottom.sourceGlobalRow) == reg.bottom.sourceRank,
                           "halo inferior = fila siguiente de su dueno: " + where);
                }
                expect(reg.localBufferCells == 7 * (reg.rowCount + 2), "buffer local: " + where);
                covered += reg.rowCount;
                messages += (reg.top.isGlobalBorder() ? 0u : 1u) + (reg.bottom.isGlobalBorder() ? 0u : 1u);
                nonEmpty += reg.empty() ? 0u : 1u;
            }
            expect(covered == h, "cobertura total: " + where);
            expect(L.haloMessagesPerStep == messages && messages == 2 * (nonEmpty - 1),
                   "mensajes por generacion = 2*(ranks con filas - 1): " + where);
            expect(L.haloBytesPerStep == messages * 5, "bytes por generacion: " + where);
            expect(owner_of_row(L, h) == kNoNeighbor, "fila fuera de rango sin dueno: " + where);
        }
    }

    // 2) Ejemplo documentado: 6 filas x 7 columnas con 4 procesos (2,2,1,1).
    {
        const auto L = describe_decomposition(7, 6, 4);
        expect(L.regions[0].top.isGlobalBorder() && L.regions[0].bottom.sourceRank == 1 &&
                   L.regions[0].bottom.sourceGlobalRow == 2,
               "6x7/4: rank0 borde arriba, halo inferior = fila 2 de r1");
        expect(L.regions[2].top.sourceGlobalRow == 3 && L.regions[2].bottom.sourceGlobalRow == 5,
               "6x7/4: rank2 recibe filas 3 y 5");
        expect(L.regions[3].bottom.isGlobalBorder(), "6x7/4: rank3 borde abajo");
        expect(L.haloMessagesPerStep == 6 && L.haloBytesPerStep == 42, "6x7/4: 6 mensajes, 42 bytes");
    }

    // 3) Ida y vuelta CSV, con y sin metricas, incluidos ranks vacios.
    {
        DecompositionSnapshot snap;
        snap.layout = describe_decomposition(9, 3, 5);  // 1,1,1,0,0
        snap.generation = 12;
        snap.haloMode = "overlap";
        for (int r = 0; r < 5; ++r) {
            snap.metrics.push_back({r, static_cast<std::size_t>(r), 0.1 * r + 1e-9, 1.0 / 3.0,
                                    static_cast<std::size_t>(2 * r), static_cast<std::size_t>(18 * r)});
        }
        std::ostringstream out;
        write_decomposition_csv(out, snap);
        std::istringstream in(out.str());
        const auto back = read_decomposition_csv(in);
        expect(back.generation == 12 && back.haloMode == "overlap", "CSV: cabecera global");
        expect(back.layout.regions.size() == 5 && back.metrics.size() == 5, "CSV: tamanos");
        bool exact = true;
        for (int r = 0; r < 5; ++r) {
            const auto& a = snap.metrics[static_cast<std::size_t>(r)];
            const auto& b = back.metrics[static_cast<std::size_t>(r)];
            exact = exact && a.rank == b.rank && a.aliveCount == b.aliveCount &&
                    a.haloSeconds == b.haloSeconds && a.computeSeconds == b.computeSeconds &&
                    a.messagesSent == b.messagesSent && a.bytesSent == b.bytesSent;
        }
        expect(exact, "CSV: metricas releidas exactas (sin redondeo)");

        DecompositionSnapshot geometryOnly;
        geometryOnly.layout = describe_decomposition(4, 10, 3);
        std::ostringstream o2;
        write_decomposition_csv(o2, geometryOnly);
        std::istringstream i2(o2.str());
        const auto g = read_decomposition_csv(i2);
        expect(g.metrics.empty() && g.haloMode.empty() && g.layout.regions.size() == 3,
               "CSV: solo geometria");

        // 4) Rechazos: la UI no debe aceptar una region inventada.
        const std::string text = out.str();
        expect_throws([&] { std::istringstream x(replace_line(text, 0, "# otra cosa")); read_decomposition_csv(x); },
                      "marca de version distinta");
        expect_throws([&] { std::istringstream x(replace_line(text, 4, "0,0,2,-1,,1,1,33,0,0,0,0,0")); read_decomposition_csv(x); },
                      "region que no coincide con la particion");
        expect_throws([&] { std::istringstream x(replace_line(text, 5, "1,1,1,0,0,2,2,33,1,0.1,,2,18")); read_decomposition_csv(x); },
                      "metricas incompletas");
        expect_throws([&] { std::istringstream x(text + "basura\n"); read_decomposition_csv(x); },
                      "datos adicionales");
        expect_throws([&] { std::istringstream x(text.substr(0, text.size() / 2)); read_decomposition_csv(x); },
                      "archivo truncado");
        expect_throws([] { describe_decomposition(0, 3, 1); }, "ancho cero");
        expect_throws([] { describe_decomposition(3, 3, 0); }, "cero procesos");
    }

    if (failures == 0) {
        std::cout << "mpi_region_metadata_tests: OK\n";
        return 0;
    }
    std::cerr << "mpi_region_metadata_tests: " << failures << " fallo(s)\n";
    return 1;
}

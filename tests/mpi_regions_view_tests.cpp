// Pruebas de la presentacion textual de regiones/halos MPI (sin MPI ni FTXUI).
#include "mpi/region_metadata.hpp"
#include "ui/mpi_regions_view.hpp"

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

bool contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const auto& l : lines) {
        if (l.find(text) != std::string::npos) return true;
    }
    return false;
}

void dump(const std::vector<std::string>& lines) {
    for (const auto& l : lines) std::cerr << "  | " << l << '\n';
}

} // namespace

int main() {
    using namespace conway;

    // 1) Ejemplo documentado: 6x7 con 4 procesos (filas 2,2,1,1).
    const auto L = mpi::describe_decomposition(7, 6, 4);
    const auto info = ui::mpiRowInfo(L);
    expect(info.size() == 6, "una entrada por fila global");
    expect(info[0].owner == 0 && !info[0].isBoundary(), "fila 0 interior de r0 (borde global arriba)");
    expect(info[1].owner == 0 && info[1].haloOfDown == 1 && info[1].haloOfUp == mpi::kNoNeighbor,
           "fila 1 viaja al halo superior de r1");
    expect(info[2].owner == 1 && info[2].haloOfUp == 0, "fila 2 viaja al halo inferior de r0");
    expect(info[4].owner == 2 && info[4].haloOfUp == 1 && info[4].haloOfDown == 3,
           "fila 4 (rank de una fila) viaja a ambos vecinos");
    expect(info[5].owner == 3 && info[5].haloOfUp == 2 && info[5].haloOfDown == mpi::kNoNeighbor,
           "fila 5 solo hacia arriba");

    const auto map = ui::mpiRowMapLines(L);
    expect(map.size() == 7, "mapa completo: cabecera + 6 filas");
    expect(contains(map, "4  r2   -> halo inf. de r1 y halo sup. de r3"), "linea de la fila 4");
    if (failures) dump(map);

    mpi::DecompositionSnapshot snap;
    snap.layout = L;
    snap.generation = 10;
    snap.haloMode = "overlap";
    const auto table = ui::mpiRegionTableLines(snap);
    expect(table.size() == 3 + 4, "tabla: 2 lineas de resumen + cabecera + 4 ranks");
    expect(contains(table, "MPI: 4 procesos | tablero 6x7 | generacion 10 | halos: overlap"),
           "cabecera de la tabla");
    expect(contains(table, "Halos por generacion: 6 mensajes, 42 bytes"), "trafico por generacion");
    expect(contains(table, "borde (0)") && contains(table, "fila 2 de r1"), "origen de halos");
    expect(!contains(table, "computo(s)"), "sin metricas no hay columnas de metricas");

    for (int r = 0; r < 4; ++r) {
        snap.metrics.push_back({r, 1, 0.5, 0.25, 2, 14});
    }
    const auto withMetrics = ui::mpiRegionTableLines(snap);
    expect(contains(withMetrics, "computo(s)") && contains(withMetrics, "0.250000"),
           "con metricas se muestran las columnas");
    ui::MpiRegionsRenderOptions noMetrics;
    noMetrics.showMetrics = false;
    expect(!contains(ui::mpiRegionTableLines(snap, noMetrics), "computo(s)"), "showMetrics=false");

    expect(ui::mpiWorkersSummary(L) == "MPI: 4 procesos (4 con filas) | halos 6 msg/gen, 42 B/gen",
           "resumen para workersInfo");

    // 2) Tablero alto: solo filas frontera y una linea de resumen por region.
    const auto big = mpi::describe_decomposition(10, 100, 4);
    const auto bigMap = ui::mpiRowMapLines(big);
    expect(bigMap.size() == 1 + 4 * 3, "mapa resumido: 3 lineas por rank");
    expect(contains(bigMap, "(23 filas interiores)"), "resumen de interiores");
    expect(contains(bigMap, "24  r0   -> halo sup. de r1"), "ultima fila de r0");

    // 3) Mas procesos que filas: ranks vacios declarados, sin halos.
    const auto sparse = mpi::describe_decomposition(9, 3, 5);
    const auto sparseMap = ui::mpiRowMapLines(sparse);
    expect(contains(sparseMap, "Ranks sin filas: 2"), "ranks vacios");
    expect(contains(ui::mpiRegionTableLines({sparse, 0, "", {}}), "(vacia)"), "region vacia en tabla");
    expect(ui::mpiWorkersSummary(sparse) == "MPI: 5 procesos (3 con filas) | halos 4 msg/gen, 36 B/gen",
           "resumen con ranks vacios");

    if (failures == 0) {
        std::cout << "mpi_regions_view_tests: OK\n";
        return 0;
    }
    std::cerr << "mpi_regions_view_tests: " << failures << " fallo(s)\n";
    return 1;
}

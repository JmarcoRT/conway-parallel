// Semana 7 - Validacion de los motores a traves de la fachada conway::Game.
//
// Solo se usa la API publica de Game (constructor desde InputData, setEngine,
// step, isAlive, setCell, clear, width/height/generation/aliveCount): no se
// toca GameState. Para cada caso se crean dos Game desde el MISMO InputData,
// uno con el motor por defecto (Sequential) y otro con Engine::OpenMP, y se
// comparan tras CADA generacion: dimensiones, generacion, aliveCount y todas
// las celdas. Se repite con 1, 2, 4 y 8 hilos.
//
// Uso: game_engine_tests [raiz_del_repo]   (por defecto ".")

#include "core/game.hpp"
#include "input/input.hpp"

#include <omp.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using conway::Engine;
using conway::Game;

int passes = 0;
int failures = 0;

void report(bool ok, const std::string& name, const std::string& detail = "") {
    if (ok) ++passes; else ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name;
    if (!detail.empty()) std::cout << ' ' << detail;
    std::cout << '\n';
}

InputData randomInput(std::size_t w, std::size_t h, unsigned seed) {
    InputData in{w, h, std::vector<std::uint8_t>(w * h)};
    std::mt19937 rng(seed);
    for (auto& v : in.cells) v = static_cast<std::uint8_t>(rng() % 100 < 35 ? 1 : 0);
    return in;
}

InputData fullInput(std::size_t w, std::size_t h) {
    return InputData{w, h, std::vector<std::uint8_t>(w * h, 1)};
}

std::size_t recount(const Game& g) {
    std::size_t n = 0;
    for (std::size_t r = 0; r < g.height(); ++r)
        for (std::size_t c = 0; c < g.width(); ++c) n += g.isAlive(r, c) ? 1 : 0;
    return n;
}

// Igualdad observable por la API publica + coherencia de aliveCount.
bool sameBoard(const Game& a, const Game& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    if (a.generation() != b.generation() || a.aliveCount() != b.aliveCount()) return false;
    if (a.aliveCount() != recount(a) || b.aliveCount() != recount(b)) return false;
    for (std::size_t r = 0; r < a.height(); ++r)
        for (std::size_t c = 0; c < a.width(); ++c)
            if (a.isAlive(r, c) != b.isAlive(r, c)) return false;
    return true;
}

// Tamano real del equipo de hilos que OpenMP crea con la configuracion actual.
int actualTeamSize() {
    int n = 0;
#pragma omp parallel
    {
#pragma omp single
        n = omp_get_num_threads();
    }
    return n;
}

void equivalence(const std::string& name, const InputData& in, int generations) {
    for (int threads : {1, 2, 4, 8}) {
        omp_set_dynamic(0);
        omp_set_num_threads(threads);
        const int team = actualTeamSize();
        const InputData before = in;

        Game seq(in);                       // motor por defecto
        Game par(in);
        par.setEngine(Engine::OpenMP);

        bool ok = (team == threads) && seq.engine() == Engine::Sequential &&
                  par.engine() == Engine::OpenMP && sameBoard(seq, par);
        int badGen = ok ? -1 : 0;
        for (int g = 1; g <= generations && ok; ++g) {
            seq.step();
            par.step();
            if (!sameBoard(seq, par)) { ok = false; badGen = g; }
        }
        // La entrada original no se modifica (ARCHITECTURE.md, seccion 7).
        const bool inputIntact = in.cells == before.cells && in.width == before.width &&
                                 in.height == before.height;
        ok = ok && inputIntact;

        std::string detail = "hilos=" + std::to_string(threads) +
            " equipo_real=" + std::to_string(team) +
            " generaciones=" + std::to_string(generations) +
            " vivas_finales=" + std::to_string(par.aliveCount());
        if (!ok) detail += " (fallo en generacion " + std::to_string(badGen) + ")";
        report(ok, "game_openmp_igual_secuencial[" + name + "]", detail);
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string root = (argc > 1) ? argv[1] : ".";
    try {
        const InputData block   = loadInput(root + "/patterns/block.txt");
        const InputData blinker = loadInput(root + "/patterns/blinker.txt");
        const InputData glider  = loadInput(root + "/patterns/glider.txt");

        // 1) Equivalencia generacion a generacion con 1, 2, 4 y 8 hilos.
        equivalence("block",   block,   4);
        equivalence("blinker", blinker, 4);
        equivalence("glider",  glider,  12);
        equivalence("aleatorio_37x53",   randomInput(37, 53, 12345), 50);
        equivalence("aleatorio_200x150", randomInput(200, 150, 777), 30);
        equivalence("lleno_16x16",       fullInput(16, 16), 5);
        equivalence("una_fila_64x1",     randomInput(64, 1, 5), 10);
        equivalence("una_columna_1x64",  randomInput(1, 64, 6), 10);

        omp_set_dynamic(0);
        omp_set_num_threads(4);

        // 2) El motor por defecto sigue siendo el secuencial.
        {
            Game g(block);
            report(g.engine() == Engine::Sequential, "motor_por_defecto_secuencial");
        }

        // 3) Casos de referencia de ARCHITECTURE.md usando solo Engine::OpenMP.
        {
            Game g(block);
            g.setEngine(Engine::OpenMP);
            for (int i = 0; i < 4; ++i) g.step();
            report(g.generation() == 4 && g.aliveCount() == 4 &&
                       g.isAlive(1, 1) && g.isAlive(1, 2) && g.isAlive(2, 1) && g.isAlive(2, 2),
                   "openmp_block_permanece_estable");
        }
        {
            Game g(blinker);
            g.setEngine(Engine::OpenMP);
            g.step();
            const bool horizontal = g.isAlive(2, 1) && g.isAlive(2, 2) && g.isAlive(2, 3) &&
                                    g.aliveCount() == 3;
            g.step();
            const bool vertical = g.isAlive(1, 2) && g.isAlive(2, 2) && g.isAlive(3, 2) &&
                                  g.aliveCount() == 3 && g.generation() == 2;
            report(horizontal && vertical, "openmp_blinker_periodo_2");
        }
        {
            Game g(glider);
            g.setEngine(Engine::OpenMP);
            for (int i = 0; i < 4; ++i) g.step();
            bool shifted = g.aliveCount() == 5;
            // El glider se desplaza (+1, +1) cada 4 generaciones.
            for (std::size_t r = 0; r < glider.height; ++r)
                for (std::size_t c = 0; c < glider.width; ++c)
                    if (glider.cells[r * glider.width + c] != 0)
                        shifted = shifted && g.isAlive(r + 1, c + 1);
            report(shifted, "openmp_glider_se_desplaza_tras_4_generaciones");
        }

        // 4) Cambiar de motor entre generaciones no altera el resultado.
        {
            const InputData in = randomInput(37, 53, 99);
            Game ref(in);
            Game mix(in);
            for (int i = 0; i < 12; ++i) ref.step();
            for (int i = 0; i < 12; ++i) {
                mix.setEngine(i % 3 == 0 ? Engine::Sequential : Engine::OpenMP);
                mix.step();
            }
            report(sameBoard(ref, mix), "cambio_de_motor_entre_generaciones");
        }

        // 5) setCell / clear conviven con el motor OpenMP.
        {
            Game ref(blinker);
            Game par(blinker);
            par.setEngine(Engine::OpenMP);
            ref.setCell(0, 0, true);
            par.setCell(0, 0, true);
            for (int i = 0; i < 3; ++i) { ref.step(); par.step(); }
            bool ok = sameBoard(ref, par);
            par.clear();
            ok = ok && par.generation() == 0 && par.aliveCount() == 0 &&
                 par.engine() == Engine::OpenMP;
            par.step();
            ok = ok && par.generation() == 1 && par.aliveCount() == 0;
            report(ok, "openmp_con_setCell_y_clear");
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }

    std::cout << "Resultado: " << passes << " PASS, " << failures << " FAIL\n";
    return failures == 0 ? 0 : 1;
}

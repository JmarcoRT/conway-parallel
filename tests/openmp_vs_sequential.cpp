// Semana 6 - Prototipo OpenMP aislado: comparacion contra el motor secuencial.
//
// Para cada caso se crean DOS core::GameState identicos a partir del mismo
// InputData. Uno avanza con step_sequential y el otro con step_openmp, y tras
// CADA generacion se comparan el area logica de `current`, `generation` y
// `aliveCount`. Se repite con 1, 2, 4 y 8 hilos (solo para verificar
// correccion; NO es un benchmark, eso corresponde a S7).
//
// Uso: openmp_vs_sequential [raiz_del_repo]   (por defecto ".")

#include "core/engines/openmp.hpp"
#include "core/engines/sequential.hpp"
#include "core/game_state.hpp"
#include "input/input.hpp"

#include <omp.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using conway::core::GameState;

// Misma construccion que conway::Game::Game (halo de 1 celda, stride = w + 2).
GameState makeState(const InputData& in) {
    GameState s{};
    s.width = in.width;
    s.height = in.height;
    s.stride = in.width + 2;
    s.current.assign(s.stride * (s.height + 2), 0);
    s.next.assign(s.stride * (s.height + 2), 0);
    for (std::size_t r = 0; r < s.height; ++r) {
        for (std::size_t c = 0; c < s.width; ++c) {
            const std::uint8_t v = in.cells[r * s.width + c];
            s.current[(r + 1) * s.stride + (c + 1)] = v;
            s.aliveCount += v;
        }
    }
    return s;
}

// Compara solo el area logica + contadores (el halo no es estado).
bool sameState(const GameState& a, const GameState& b) {
    if (a.generation != b.generation || a.aliveCount != b.aliveCount) return false;
    for (std::size_t r = 1; r <= a.height; ++r)
        for (std::size_t c = 1; c <= a.width; ++c)
            if (a.current[r * a.stride + c] != b.current[r * b.stride + c]) return false;
    return true;
}

InputData randomInput(std::size_t w, std::size_t h, unsigned seed) {
    InputData in{w, h, std::vector<std::uint8_t>(w * h)};
    std::mt19937 rng(seed);
    for (auto& v : in.cells) v = static_cast<std::uint8_t>(rng() % 100 < 35 ? 1 : 0);
    return in;
}

int failures = 0;
int passes = 0;

void runCase(const std::string& name, const InputData& in, int generations) {
    for (int threads : {1, 2, 4, 8}) {
        omp_set_num_threads(threads);
        GameState seq = makeState(in);
        GameState par = makeState(in);
        bool ok = true;
        int badGen = -1;
        for (int g = 1; g <= generations && ok; ++g) {
            conway::core::step_sequential(seq);
            conway::core::step_openmp(par);
            if (!sameState(seq, par)) { ok = false; badGen = g; }
        }
        if (ok) {
            ++passes;
            std::cout << "PASS " << name << " hilos=" << threads
                      << " generaciones=" << generations
                      << " vivas_finales=" << par.aliveCount << '\n';
        } else {
            ++failures;
            std::cout << "FAIL " << name << " hilos=" << threads
                      << ": difiere en la generacion " << badGen << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string root = (argc > 1) ? argv[1] : ".";
    try {
        runCase("block",   loadInput(root + "/patterns/block.txt"),   4);
        runCase("blinker", loadInput(root + "/patterns/blinker.txt"), 4);
        runCase("glider",  loadInput(root + "/patterns/glider.txt"),  12);
        // Tableros aleatorios con semilla fija (reproducible). Dimensiones no
        // divisibles entre los hilos ejercitan el reparto desigual de filas.
        runCase("aleatorio_37x53",   randomInput(37, 53, 12345), 50);
        runCase("aleatorio_200x150", randomInput(200, 150, 777), 30);
        // Casos limite del reparto: una sola fila (mas hilos que filas) y una sola columna.
        runCase("una_fila_64x1",     randomInput(64, 1, 5), 10);
        runCase("una_columna_1x64",  randomInput(1, 64, 6), 10);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }

    // Comportamiento esperado por ARCHITECTURE.md: Blinker vuelve a su estado
    // inicial tras 2 generaciones usando solo el motor OpenMP.
    {
        GameState s = makeState(loadInput(root + "/patterns/blinker.txt"));
        const auto initial = s.current;
        omp_set_num_threads(4);
        conway::core::step_openmp(s);
        const bool changed = (s.current != initial);
        conway::core::step_openmp(s);
        const bool back = (s.current == initial);
        if (changed && back) { ++passes; std::cout << "PASS blinker_periodo_2_openmp\n"; }
        else { ++failures; std::cout << "FAIL blinker_periodo_2_openmp\n"; }
    }

    std::cout << "Resultado: " << passes << " PASS, " << failures << " FAIL\n";
    return failures == 0 ? 0 : 1;
}

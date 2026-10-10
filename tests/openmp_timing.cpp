// Semana 7 - Medicion INICIAL (exploratoria) del motor OpenMP via conway::Game.
//
// No es el benchmark final: no exporta CSV, no calcula speedup, eficiencia ni
// throughput. Solo mide el tiempo de pared de una configuracion fija para el
// motor secuencial y para OpenMP con 1, 2, 4 y 8 hilos.
//
// Metodo:
//   - Mismo tablero (aleatorio, semilla fija) y mismo numero de generaciones
//     en todas las configuraciones; cada ejecucion crea su propio Game desde
//     el mismo InputData.
//   - La region temporizada contiene SOLO el bucle de game.step() (sin
//     construccion del Game, sin validacion, sin E/S).
//   - Una ejecucion de calentamiento sin medir y luego R repeticiones medidas.
//   - Tiempo representativo = mediana de las R repeticiones (tambien se
//     muestran minimo y maximo).
//   - Fuera de la region temporizada se comprueba que el estado final
//     (generacion, aliveCount y checksum del tablero) coincide con el del
//     motor secuencial.
//
// Uso: openmp_timing [--width W] [--height H] [--generations G] [--repeats R]
//                    [--seed S]

#include "core/game.hpp"
#include "input/input.hpp"

#include <omp.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using conway::Engine;
using conway::Game;

struct Config {
    std::size_t width = 1000;
    std::size_t height = 1000;
    int generations = 100;
    int repeats = 5;
    unsigned seed = 2026;
    unsigned densityPercent = 35;
};

InputData makeInput(const Config& cfg) {
    InputData in{cfg.width, cfg.height, std::vector<std::uint8_t>(cfg.width * cfg.height)};
    std::mt19937 rng(cfg.seed);
    for (auto& v : in.cells) v = static_cast<std::uint8_t>(rng() % 100 < cfg.densityPercent ? 1 : 0);
    return in;
}

// FNV-1a sobre dimensiones, generacion, aliveCount y celdas.
std::uint64_t fingerprint(const Game& g) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(g.width()); mix(g.height()); mix(g.generation()); mix(g.aliveCount());
    for (std::size_t r = 0; r < g.height(); ++r)
        for (std::size_t c = 0; c < g.width(); ++c) mix(g.isAlive(r, c) ? 1 : 0);
    return h;
}

struct Run {
    double ms;
    std::uint64_t fingerprint;
    std::size_t alive;
};

Run runOnce(const InputData& in, Engine engine, int threads, int generations) {
    Game game(in);                       // fuera de la region temporizada
    game.setEngine(engine);
    omp_set_num_threads(threads);

    const auto t0 = Clock::now();
    for (int g = 0; g < generations; ++g) game.step();
    const auto t1 = Clock::now();

    return Run{std::chrono::duration<double, std::milli>(t1 - t0).count(),
               fingerprint(game), game.aliveCount()};  // validacion fuera del tiempo
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

std::size_t parseSize(const std::string& flag, const char* text) {
    char* end = nullptr;
    const long long v = std::strtoll(text, &end, 10);
    if (end == text || *end != '\0' || v <= 0)
        throw std::invalid_argument("Valor invalido para " + flag + ": " + text);
    return static_cast<std::size_t>(v);
}

Config parseArgs(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (i + 1 >= argc) throw std::invalid_argument("Falta el valor de " + flag);
        const char* value = argv[++i];
        if (flag == "--width") cfg.width = parseSize(flag, value);
        else if (flag == "--height") cfg.height = parseSize(flag, value);
        else if (flag == "--generations") cfg.generations = static_cast<int>(parseSize(flag, value));
        else if (flag == "--repeats") cfg.repeats = static_cast<int>(parseSize(flag, value));
        else if (flag == "--seed") cfg.seed = static_cast<unsigned>(parseSize(flag, value));
        else throw std::invalid_argument("Opcion desconocida: " + flag);
    }
    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Config cfg = parseArgs(argc, argv);
        const InputData input = makeInput(cfg);
        omp_set_dynamic(0);

        std::cout << "Medicion inicial OpenMP (exploratoria, no es el benchmark final)\n"
                  << "Tablero: " << cfg.width << "x" << cfg.height << " aleatorio, densidad "
                  << cfg.densityPercent << "%, semilla " << cfg.seed << '\n'
                  << "Generaciones por ejecucion: " << cfg.generations << '\n'
                  << "Repeticiones medidas: " << cfg.repeats
                  << " (mas 1 de calentamiento sin medir); tiempo representativo = mediana\n"
                  << "Procesadores visibles para OpenMP: " << omp_get_num_procs() << "\n\n";

        struct Item { const char* label; Engine engine; int threads; };
        const std::vector<Item> items = {
            {"secuencial", Engine::Sequential, 1},
            {"openmp", Engine::OpenMP, 1},
            {"openmp", Engine::OpenMP, 2},
            {"openmp", Engine::OpenMP, 4},
            {"openmp", Engine::OpenMP, 8},
        };

        std::cout << std::left << std::setw(12) << "motor" << std::setw(8) << "hilos"
                  << std::right << std::setw(12) << "mediana_ms" << std::setw(12) << "min_ms"
                  << std::setw(12) << "max_ms" << "  identico_al_secuencial\n";

        bool allEqual = true;
        std::uint64_t refFingerprint = 0;
        std::size_t refAlive = 0;
        std::vector<std::string> detail;

        for (std::size_t k = 0; k < items.size(); ++k) {
            const Item& it = items[k];
            (void)runOnce(input, it.engine, it.threads, cfg.generations);   // calentamiento
            std::vector<double> times;
            bool same = true;
            for (int r = 0; r < cfg.repeats; ++r) {
                const Run run = runOnce(input, it.engine, it.threads, cfg.generations);
                times.push_back(run.ms);
                if (k == 0 && r == 0) { refFingerprint = run.fingerprint; refAlive = run.alive; }
                if (run.fingerprint != refFingerprint || run.alive != refAlive) same = false;
            }
            allEqual = allEqual && same;

            std::cout << std::left << std::setw(12) << it.label << std::setw(8) << it.threads
                      << std::right << std::fixed << std::setprecision(1)
                      << std::setw(12) << median(times)
                      << std::setw(12) << *std::min_element(times.begin(), times.end())
                      << std::setw(12) << *std::max_element(times.begin(), times.end())
                      << "  " << (same ? "SI" : "NO") << '\n';

            std::string line = std::string(it.label) + " hilos=" + std::to_string(it.threads) + " tiempos_ms:";
            for (double t : times) {
                std::ostringstream os; os << std::fixed << std::setprecision(1) << ' ' << t;
                line += os.str();
            }
            detail.push_back(line);
        }

        std::cout << "\nTiempos de cada repeticion (ms):\n";
        for (const auto& l : detail) std::cout << "  " << l << '\n';
        std::cout << "\nCelulas vivas finales (referencia secuencial): " << refAlive << '\n';
        std::cout << "Resultado: " << (allEqual ? "OK, todos los resultados coinciden con el secuencial"
                                                : "FALLO, algun resultado difiere del secuencial") << '\n';
        return allEqual ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }
}

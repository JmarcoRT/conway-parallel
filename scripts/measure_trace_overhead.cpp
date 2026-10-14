// Medicion EXPLORATORIA del overhead del camino no instrumentado frente
// al camino instrumentado (StepTraceSink no-op). Compara el mismo numero
// de generaciones con y sin sink, en ambos motores.
// NO usa umbrales rigidos: la salida son milisegundos medianos y una
// relacion orientativa. El caller decide si la diferencia es aceptable.
// Este archivo NO forma parte de la suite de regresion: se compila y
// ejecuta bajo demanda desde scripts/measure_trace_overhead.sh. Los
// resultados dependen del hardware y del entorno (WSL/SMT, frecuencia,
// carga del sistema) y son solo orientativos.

#include "core/game.hpp"
#include "core/step_trace.hpp"
#include "input/input.hpp"

#include <omp.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

InputData makeInput(std::size_t w, std::size_t h, unsigned seed) {
    InputData in{w, h, std::vector<std::uint8_t>(w * h, 0)};
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 99);
    for (auto& v : in.cells) v = (dist(rng) < 35) ? std::uint8_t{1} : std::uint8_t{0};
    return in;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    if (n == 0) return 0.0;
    return (n % 2 == 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

struct SinkNothing : conway::core::StepTraceSink {
    void onStepComplete(conway::core::StepTrace&&) override {}
};

double measure(conway::Game g, int gens, int repeats, bool with_sink) {
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(repeats));
    for (int r = 0; r < repeats; ++r) {
        const auto t0 = Clock::now();
        for (int k = 0; k < gens; ++k) {
            if (with_sink) {
                SinkNothing s;
                g.step(&s);
            } else {
                g.step();
            }
        }
        const auto t1 = Clock::now();
        times.push_back(
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return median(times);
}

}  // namespace

int main() {
    omp_set_dynamic(0);

    struct Config {
        std::size_t width;
        std::size_t height;
        int generations;
        int threads;
    };
    const std::vector<Config> configs = {
        {256, 256, 50, 1},
        {256, 256, 50, 2},
        {256, 256, 50, 4},
        {256, 256, 50, 8},
        {512, 512, 30, 4},
        {512, 512, 30, 8},
    };

    const int repeats = 7;
    std::cout << "Overhead orientativo del camino instrumentado (sink=no-op).\n"
              << "Repeticiones: " << repeats << "; tiempo = mediana (ms).\n"
              << "Ratio > 1.0 significa que el camino instrumentado tarda mas.\n"
              << "NO es un test de rendimiento: la variabilidad entre runs puede\n"
              << "ser grande en WSL y maquinas con SMT; la relacion se da solo\n"
              << "como orientacion.\n\n";

    std::cout << std::left
              << std::setw(6)  << "engine"
              << std::setw(8)  << "w"
              << std::setw(8)  << "h"
              << std::setw(6)  << "gens"
              << std::setw(6)  << "thr"
              << std::right
              << std::setw(12) << "no_sink_ms"
              << std::setw(12) << "with_sink_ms"
              << std::setw(10) << "ratio"
              << '\n';

    for (const auto& cfg : configs) {
        const InputData in = makeInput(cfg.width, cfg.height, 1234);
        omp_set_num_threads(cfg.threads);

        conway::Game gSeq(in);
        gSeq.setEngine(conway::Engine::Sequential);
        const double seqNoSink  = measure(gSeq, cfg.generations, repeats, false);
        const double seqWithSnk = measure(gSeq, cfg.generations, repeats, true);
        std::cout << std::left
                  << std::setw(6)  << "seq"
                  << std::setw(8)  << cfg.width
                  << std::setw(8)  << cfg.height
                  << std::setw(6)  << cfg.generations
                  << std::setw(6)  << cfg.threads
                  << std::right
                  << std::setw(12) << std::fixed << std::setprecision(3) << seqNoSink
                  << std::setw(12) << std::fixed << std::setprecision(3) << seqWithSnk
                  << std::setw(10) << std::fixed << std::setprecision(3)
                                     << (seqNoSink > 0 ? seqWithSnk / seqNoSink : 0.0)
                  << '\n';

        conway::Game gOmp(in);
        gOmp.setEngine(conway::Engine::OpenMP);
        const double ompNoSink  = measure(gOmp, cfg.generations, repeats, false);
        const double ompWithSnk = measure(gOmp, cfg.generations, repeats, true);
        std::cout << std::left
                  << std::setw(6)  << "omp"
                  << std::setw(8)  << cfg.width
                  << std::setw(8)  << cfg.height
                  << std::setw(6)  << cfg.generations
                  << std::setw(6)  << cfg.threads
                  << std::right
                  << std::setw(12) << std::fixed << std::setprecision(3) << ompNoSink
                  << std::setw(12) << std::fixed << std::setprecision(3) << ompWithSnk
                  << std::setw(10) << std::fixed << std::setprecision(3)
                                     << (ompNoSink > 0 ? ompWithSnk / ompNoSink : 0.0)
                  << '\n';
    }

    return 0;
}

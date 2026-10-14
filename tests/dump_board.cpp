// Utilidad de apoyo para el criterio de equivalencia (docs/TESTING.md).
//
// No reemplaza a `main` ni a ningun motor: unicamente carga un tablero con
// el `input` existente, lo avanza N generaciones usando `conway::Game` (sin
// duplicar su algoritmo) y escribe el resultado en un formato de texto
// simple y estable, pensado para compararse con tests/compare_boards.py.
//
// Formato de salida en stdout (contrato estable, NO depende del motor):
//   linea 1: "<ancho> <alto> <generacion> <vivas>"
//   lineas siguientes: <alto> filas de <ancho> caracteres '0'/'1'
//
// Uso:
//   dump_board <archivo_de_entrada> <generaciones>
//              [--engine seq|openmp] [--require-threads N]
//
//   --engine seq       motor secuencial (por defecto; comportamiento original).
//   --engine openmp    Game::setEngine(Engine::OpenMP). El numero de hilos NO
//                      se fija aqui: lo controla quien invoca mediante
//                      OMP_NUM_THREADS (ver docs/OPENMP_INTEGRATION.md).
//   --require-threads  solo con --engine openmp: antes de evolucionar mide el
//                      tamano real del equipo OpenMP (region paralela de
//                      prueba) y termina con codigo 3 si no es N. Evita que
//                      una validacion "con 8 hilos" corra en realidad con
//                      menos (p. ej. por OMP_THREAD_LIMIT) y pase sin ejercitar
//                      el paralelismo pedido.
//
// Metadatos de la ejecucion (motor, hilos): SOLO en stderr, con prefijo "# ".
// stdout es identico entre motores cuando el resultado es equivalente.
//
// Codigos de salida: 0 ok; 1 error de ejecucion; 2 uso incorrecto;
// 3 equipo OpenMP distinto del exigido; 4 aliveCount() del motor no coincide
// con el recuento celda a celda del tablero volcado.

#include "core/game.hpp"
#include "input/input.hpp"

#include <omp.h>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

// Tamano del equipo que el runtime concede a una region paralela. Es una
// sonda independiente de step_openmp: indica que el entorno permite N hilos,
// no instrumenta el interior del motor.
int probeTeamSize() {
    int team = 0;
#pragma omp parallel
    {
#pragma omp single
        team = omp_get_num_threads();
    }
    return team;
}

int usage() {
    std::cerr << "Uso: dump_board <archivo_de_entrada> <generaciones> "
                 "[--engine seq|openmp] [--require-threads N]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) return usage();

        const std::string path = argv[1];
        const int generations = std::stoi(argv[2]);
        if (generations < 0) {
            std::cerr << "El numero de generaciones no puede ser negativo\n";
            return 2;
        }

        conway::Engine engine = conway::Engine::Sequential;
        int requiredThreads = 0;  // 0 = sin exigencia
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--engine" && i + 1 < argc) {
                const std::string value = argv[++i];
                if (value == "seq") {
                    engine = conway::Engine::Sequential;
                } else if (value == "openmp") {
                    engine = conway::Engine::OpenMP;
                } else {
                    std::cerr << "Motor desconocido: " << value << '\n';
                    return usage();
                }
            } else if (arg == "--require-threads" && i + 1 < argc) {
                requiredThreads = std::stoi(argv[++i]);
                if (requiredThreads < 1) return usage();
            } else {
                return usage();
            }
        }
        if (requiredThreads > 0 && engine != conway::Engine::OpenMP) {
            std::cerr << "--require-threads solo aplica con --engine openmp\n";
            return usage();
        }

        InputData input = loadInput(path);
        conway::Game game(input);
        game.setEngine(engine);

        if (engine == conway::Engine::OpenMP) {
            const int team = probeTeamSize();
            std::cerr << "# engine=openmp omp_max_threads=" << omp_get_max_threads()
                      << " equipo_real=" << team << '\n';
            if (requiredThreads > 0 && team != requiredThreads) {
                std::cerr << "El equipo OpenMP real (" << team
                          << ") no coincide con el exigido (" << requiredThreads
                          << "); revisar OMP_NUM_THREADS/OMP_THREAD_LIMIT/OMP_DYNAMIC\n";
                return 3;
            }
        } else {
            std::cerr << "# engine=seq\n";
        }

        for (int i = 0; i < generations; ++i) {
            game.step();
        }

        // Se arma el volcado y se recuentan las vivas celda a celda para no
        // fiarse unicamente del contador interno del motor (aliveCount()).
        std::string rows;
        rows.reserve((game.width() + 1) * game.height());
        std::size_t recount = 0;
        for (std::size_t row = 0; row < game.height(); ++row) {
            for (std::size_t col = 0; col < game.width(); ++col) {
                const bool alive = game.isAlive(row, col);
                rows.push_back(alive ? '1' : '0');
                recount += alive ? 1 : 0;
            }
            rows.push_back('\n');
        }
        if (recount != game.aliveCount()) {
            std::cerr << "aliveCount() del motor (" << game.aliveCount()
                      << ") no coincide con el recuento del tablero (" << recount << ")\n";
            return 4;
        }

        std::cout << game.width() << ' ' << game.height() << ' '
                  << game.generation() << ' ' << game.aliveCount() << '\n'
                  << rows;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

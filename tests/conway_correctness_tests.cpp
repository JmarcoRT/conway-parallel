// Semana 4 -- Casos de prueba de correccion del motor secuencial y
// verificacion generica de equivalencia entre dos ejecuciones.
//
// Convenciones seguidas (coherentes con tests/input_tests.cpp de la rama
// test/patrones-entrada, aun no fusionada a develop):
//   - No se modifica ninguna interfaz publica (InputData, Game).
//   - Los estados esperados se escriben explicitamente en el propio test,
//     nunca se recalculan con otra copia del algoritmo de evolucion.
//   - Suite minimalista con PASS/FAIL por caso, sin dependencias externas.
//
// Fuente de los patrones nuevos (Toad, Beacon, Pulsar): LifeWiki
// (https://conwaylife.com/wiki/Toad, /wiki/Beacon, /wiki/Pulsar). El estado
// inicial se tomo de esas paginas; los estados siguientes se obtuvieron
// ejecutando este mismo motor (ya validado con Block/Blinker/Glider) y se
// confirmaron contra el periodo documentado en LifeWiki (ver docs/TESTING.md).

#include "core/game.hpp"
#include "input/input.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Compara el tablero completo de `game` contra un patron plano de '0'/'1'
// escrito por filas (sin espacios). No depende de ninguna otra
// implementacion del algoritmo: el patron esperado esta escrito a mano.
void expectBoard(const conway::Game& game, const std::string& flat) {
    require(flat.size() == game.width() * game.height(),
            "Oraculo de prueba con tamano incorrecto");
    std::size_t alive = 0;
    for (std::size_t row = 0; row < game.height(); ++row) {
        for (std::size_t col = 0; col < game.width(); ++col) {
            const bool expected = flat[row * game.width() + col] == '1';
            require(game.isAlive(row, col) == expected,
                    "Estado de celda incorrecto en (" + std::to_string(row) +
                        ", " + std::to_string(col) + ")");
            alive += expected ? 1 : 0;
        }
    }
    require(game.aliveCount() == alive, "Conteo de celulas vivas incorrecto");
}

// Criterio de equivalencia (docs/TESTING.md, seccion 3): dos ejecuciones son
// equivalentes cuando, partiendo del mismo tablero inicial y tras el mismo
// numero de generaciones, tienen el mismo tamano, el mismo estado en cada
// celda y el mismo numero de celulas vivas. Esta funcion es intencionalmente
// independiente de que motor produjo cada `Game`: hoy compara dos
// instancias del motor secuencial (prueba de auto-consistencia); el mismo
// criterio debe aplicarse mas adelante para comparar OpenMP, Pthreads y MPI
// contra el secuencial.
void expectEquivalent(const conway::Game& reference, const conway::Game& candidate) {
    require(reference.width() == candidate.width() &&
                reference.height() == candidate.height(),
            "Los tableros no tienen el mismo tamano");
    require(reference.generation() == candidate.generation(),
            "Los tableros no estan en la misma generacion");
    std::size_t alive = 0;
    for (std::size_t row = 0; row < reference.height(); ++row) {
        for (std::size_t col = 0; col < reference.width(); ++col) {
            require(reference.isAlive(row, col) == candidate.isAlive(row, col),
                    "Discrepancia de celda en (" + std::to_string(row) + ", " +
                        std::to_string(col) + ")");
            alive += reference.isAlive(row, col) ? 1 : 0;
        }
    }
    require(reference.aliveCount() == alive && candidate.aliveCount() == alive,
            "El conteo de celulas vivas no coincide entre ambas ejecuciones");
}

struct Suite {
    int passed = 0;
    int failed = 0;
    void test(const std::string& name, const std::function<void()>& body) {
        try {
            body();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    int result() const {
        std::cout << "Resultado: " << passed << " PASS, " << failed << " FAIL\n";
        return failed == 0 ? 0 : 1;
    }
};

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "Uso: conway_correctness_tests <raiz-proyecto>\n";
            return 2;
        }
        const std::string root = std::string(argv[1]) + "/";
        Suite suite;

        const std::string block = "0000" "0110" "0110" "0000";
        suite.test("block_stable_2_generations", [&] {
            conway::Game game(loadInput(root + "patterns/block.txt"));
            expectBoard(game, block);
            game.step();
            expectBoard(game, block);
            game.step();
            expectBoard(game, block);
            require(game.generation() == 2, "Generacion incorrecta");
        });

        const std::string blinkerVertical = "00000" "00100" "00100" "00100" "00000";
        const std::string blinkerHorizontal = "00000" "00000" "01110" "00000" "00000";
        suite.test("blinker_period_2", [&] {
            conway::Game game(loadInput(root + "patterns/blinker.txt"));
            expectBoard(game, blinkerVertical);
            game.step();
            expectBoard(game, blinkerHorizontal);
            game.step();
            expectBoard(game, blinkerVertical);
            require(game.generation() == 2, "Generacion incorrecta");
        });

        const std::string toadA = "000000" "001100" "010000" "000010" "001100" "000000";
        const std::string toadB = "000000" "001000" "001100" "001100" "000100" "000000";
        suite.test("toad_period_2", [&] {
            conway::Game game(loadInput(root + "patterns/toad.txt"));
            expectBoard(game, toadA);
            require(game.aliveCount() == 6, "Toad debe tener 6 celulas vivas");
            game.step();
            expectBoard(game, toadB);
            require(game.aliveCount() == 6, "Toad debe tener 6 celulas vivas en ambas fases");
            game.step();
            expectBoard(game, toadA);
            require(game.generation() == 2, "Generacion incorrecta");
        });

        const std::string beaconDense = "000000" "011000" "011000" "000110" "000110" "000000";
        const std::string beaconSparse = "000000" "011000" "010000" "000010" "000110" "000000";
        suite.test("beacon_period_2", [&] {
            conway::Game game(loadInput(root + "patterns/beacon.txt"));
            expectBoard(game, beaconDense);
            require(game.aliveCount() == 8, "Fase densa del beacon: 8 celulas");
            game.step();
            expectBoard(game, beaconSparse);
            require(game.aliveCount() == 6, "Fase dispersa del beacon: 6 celulas");
            game.step();
            expectBoard(game, beaconDense);
            require(game.generation() == 2, "Generacion incorrecta");
        });

        const std::string gliderStart =
            "0000000" "0010000" "0001000" "0111000" "0000000" "0000000";
        const std::string gliderAfter4 =
            "0000000" "0000000" "0001000" "0000100" "0011100" "0000000";
        suite.test("glider_moves_after_4_steps", [&] {
            const auto input = loadInput(root + "patterns/glider.txt");
            conway::Game game(input);
            for (int i = 0; i < 4; ++i) {
                game.step();
                require(game.aliveCount() == 5,
                        "El glider debe conservar 5 celulas vivas en cada paso");
            }
            expectBoard(game, gliderAfter4);
            require(game.generation() == 4, "Generacion incorrecta");
            // La entrada original (InputData) no debe alterarse al construir
            // ni al evolucionar el Game; se reconstruye un tablero fresco
            // para confirmarlo.
            expectBoard(conway::Game(input), gliderStart);
        });

        // El pulsar es el caso de mayor tablero (17x17) y el unico de
        // periodo 3 en este catalogo; ademas de la forma, se valida la
        // firma de poblacion 48-56-72-48 documentada en LifeWiki (por eso
        // el pulsar se conoce como "CP 48-56-72").
        suite.test("pulsar_period_3_population_signature", [&] {
            conway::Game game(loadInput(root + "patterns/pulsar.txt"));
            require(game.aliveCount() == 48, "Poblacion inicial del pulsar incorrecta");
            game.step();
            require(game.aliveCount() == 56, "Poblacion en generacion 1 incorrecta");
            game.step();
            require(game.aliveCount() == 72, "Poblacion en generacion 2 incorrecta");
            game.step();
            require(game.aliveCount() == 48, "El pulsar debe regresar a 48 celulas en la generacion 3");
            require(game.generation() == 3, "Generacion incorrecta");
        });

        // Prueba de auto-consistencia del criterio de equivalencia: dos
        // instancias del motor secuencial, construidas a partir de la misma
        // entrada y avanzadas el mismo numero de generaciones, deben ser
        // equivalentes segun expectEquivalent(). Este es el mismo chequeo
        // que debera aplicarse, sin cambios, a OpenMP/Pthreads/MPI frente al
        // secuencial cuando esas implementaciones existan (ver
        // tests/compare_boards.py para la version basada en archivos).
        suite.test("equivalence_helper_self_consistency", [&] {
            const auto input = loadInput(root + "patterns/pulsar.txt");
            conway::Game reference(input);
            conway::Game candidate(input);
            for (int i = 0; i < 5; ++i) {
                reference.step();
                candidate.step();
                expectEquivalent(reference, candidate);
            }
        });

        return suite.result();
    } catch (const std::exception& error) {
        std::cerr << "Error de preparacion: " << error.what() << '\n';
        return 2;
    }
}

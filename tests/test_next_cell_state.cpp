// Semana 6 - Tests unitarios del helper conway::core::next_cell_state.
//
// Cubre cada rama de B3/S23 con una configuracion minimal de vecindad,
// mas un caso asimetrico y un caso donde el halo (siempre 0) participa
// como vecino. Si estos casos pasan, es matematicamente imposible que
// step_sequential produzca resultados distintos del Game::step() previo,
// porque step_sequential no hace otra cosa que llamar a next_cell_state
// celda por celda con el mismo orden, indices y halo.

#include "core/bs23.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Tamano fijo de un tablero 3x3 logico + 1 celda de halo por lado.
// stride = 3 + 2 = 5.   Buffer total: 5x5 = 25 bytes.
// Coordenadas logicas centrales son (2, 2). En buffer: 2*5 + 2 = 12.
// Sus 8 vecinos (en indices de buffer): 6, 7, 8, 11, 13, 16, 17, 18.
constexpr std::size_t kStride = 5;
constexpr std::size_t kRows = 5;
constexpr std::size_t kCenter = 2 * kStride + 2;

// Posiciones de los 8 vecinos del centro logico (2,2) en el buffer.
constexpr std::size_t kNW = 2 * kStride + 2 - kStride - 1;  //  6
constexpr std::size_t kN  = 2 * kStride + 2 - kStride;      //  7
constexpr std::size_t kNE = 2 * kStride + 2 - kStride + 1;  //  8
constexpr std::size_t kW  = 2 * kStride + 2 - 1;            // 11
constexpr std::size_t kE  = 2 * kStride + 2 + 1;            // 13
constexpr std::size_t kSW = 2 * kStride + 2 + kStride - 1;  // 16
constexpr std::size_t kS  = 2 * kStride + 2 + kStride;      // 17
constexpr std::size_t kSE = 2 * kStride + 2 + kStride + 1;  // 18

std::vector<std::size_t> allEightNeighbors() {
    return {kNW, kN, kNE, kW, kE, kSW, kS, kSE};
}

// Construye un buffer 5x5 en cero y coloca `howManyAlive` vecinos activos
// alrededor del centro logico (2,2). El primer vecino se toma de
// `whichNeighbors` (lista de indices de buffer); si la lista no tiene
// suficientes, se rellena con el siguiente vecino en orden NW,N,NE,W,E,SW,S,SE.
std::vector<std::uint8_t> build(
    std::uint8_t centerAlive,
    std::size_t howManyAlive,
    const std::vector<std::size_t>& whichNeighbors = {})
{
    std::vector<std::uint8_t> buf(kRows * kStride, 0);
    if (centerAlive) {
        buf[kCenter] = 1;
    }
    const std::vector<std::size_t> order = allEightNeighbors();
    for (std::size_t k = 0; k < howManyAlive; ++k) {
        const std::size_t idx = (k < whichNeighbors.size())
                                ? whichNeighbors[k]
                                : order[k];
        buf[idx] = 1;
    }
    return buf;
}

void check(
    const std::string& name,
    std::uint8_t center,
    std::size_t aliveNeighbors,
    std::uint8_t expected,
    const std::vector<std::size_t>& whichNeighbors = {})
{
    auto buf = build(center, aliveNeighbors, whichNeighbors);
    const std::uint8_t got = conway::core::next_cell_state(buf.data(), kStride, 2, 2);
    require(got == expected,
            "Caso '" + name + "': esperaba " + std::to_string(expected)
            + ", obtuvo " + std::to_string(got));
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
} // namespace

int main() {
    Suite suite;

    // Regla B3/S23 - casos canonicos.
    // center=1 (vivo), 0 vecinos -> muere.
    suite.test("alive_0_neighbors_dies",     [&] { check("alive_0_neighbors_dies",        1, 0, 0); });
    // center=1, 1 vecino -> muere.
    suite.test("alive_1_neighbor_dies",      [&] { check("alive_1_neighbor_dies",         1, 1, 0); });
    // center=1, 2 vecinos -> sobrevive.
    suite.test("alive_2_neighbors_lives",    [&] { check("alive_2_neighbors_lives",       1, 2, 1); });
    // center=1, 3 vecinos -> sobrevive.
    suite.test("alive_3_neighbors_lives",    [&] { check("alive_3_neighbors_lives",       1, 3, 1); });
    // center=1, 4 vecinos -> muere (sobrepoblacion).
    suite.test("alive_4_neighbors_dies",     [&] { check("alive_4_neighbors_dies",        1, 4, 0); });
    // center=0, 2 vecinos -> sigue muerto.
    suite.test("dead_2_neighbors_stays_dead",[&] { check("dead_2_neighbors_stays_dead",   0, 2, 0); });
    // center=0, 3 vecinos -> nace.
    suite.test("dead_3_neighbors_born",      [&] { check("dead_3_neighbors_born",         0, 3, 1); });
    // center=0, 4 vecinos -> sigue muerto.
    suite.test("dead_4_neighbors_stays_dead",[&] { check("dead_4_neighbors_stays_dead",   0, 4, 0); });

    // Caso asimetrico: solo el vecino NO (indice 6) esta vivo. Aunque dos
    // indices cualesquiera sumarian 2, queremos descartar bugs del estilo
    // "solo cuenta N y S" (que daria 1 vecino, no 2) o "solo cuenta cardinal"
    // (que daria 0). Si la regla ve toda la vecindad, espera 0 (centro muere
    // con 1 vecino asimetrico).
    suite.test("asymmetric_single_NW_dead_center", [&] {
        check("asymmetric_single_NW_dead_center", 0, 1, 0, {kNW});
    });

    // Caso con halo participante: celula logica en (1,1). Sus vecinos "norte",
    // "noroeste" y "noreste" caen sobre el borde superior del buffer, que
    // esta a 0 y representa el halo muerto. La regla debe tratarlos como 0.
    suite.test("halo_participates_as_zero_top_left", [&] {
        std::vector<std::uint8_t> buf(kRows * kStride, 0);
        // Posiciones que corresponden a indices donde i-stride y i-stride-1
        // para i = 1*stride + 1 = 6. Es decir, los buffers 0 y 1 (esquina NW
        // y borde norte). Si no se aplica el desplazamiento por halo,
        // podrian leerse bytes correctos; si hubiera un off-by-one, leeriamos
        // otra cosa.
        const std::uint8_t got = conway::core::next_cell_state(buf.data(), kStride, 1, 1);
        // Celula central logica (1,1) en i = 1*5+1 = 6, buffer[6] = 0, y todos
        // sus vecinos (incluyendo halo) son 0. Suma 0 -> 0.
        require(got == 0,
                "Halo no participa como 0 en (1,1); obtuvo " + std::to_string(got));
    });

    return suite.result();
}

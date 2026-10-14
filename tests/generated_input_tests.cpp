#include "input/generated_input.hpp"
#include "core/game.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <omp.h>

namespace {
int failures = 0;
void check(bool condition, const char* label) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
template<class Exception, class Function>
void rejects(Function action, const char* label) {
    try { action(); check(false, label); }
    catch (const Exception&) {}
    catch (...) { check(false, label); }
}
}

int main() {
    using namespace conway::input;
    const auto first = generateInput({4, 3, 3000, 2026});
    const auto again = generateInput({4, 3, 3000, 2026});
    check(first.input.cells == again.input.cells, "deterministic repeated generation");
    check(first.input.cells == std::vector<std::uint8_t>{1,1,1,0,0,1,0,1,0,0,0,0},
          "v1 golden row-major board, seed 2026");
    const auto zero = generateInput({4, 3, 5000, 0});
    check(zero.input.cells == std::vector<std::uint8_t>{0,0,1,1,0,0,0,0,1,0,1,0},
          "v1 golden board, seed zero");
    // Standard mt19937 first output for this seed is 4294966784 (rejected).
    // Accepted remainders: 6286, 293, 4942, 419, 4151, 6324.
    check(generateInput({3, 2, 6500, 2114088}).input.cells
              == std::vector<std::uint8_t>{1,1,1,1,1,1}, "golden rejection path");
    check(generateInput({1, 1, 10000, 0}).input.cells == std::vector<std::uint8_t>{1},
          "minimum dimensions");
    for (int density : {0, 10000}) {
        const auto board = generateInput({13, 7, density, 0});
        check(std::all_of(board.input.cells.begin(), board.input.cells.end(),
                         [density](auto cell) { return cell == density / 10000; }),
              "density endpoints");
        check(board.metadata.observed_density == density / 10000.0, "endpoint metadata");
    }
    const auto large = generateInput({2000, 2000, 3000, 42});
    check(large.input.cells.size() == 4000000, "large board cell count");
    check(std::all_of(large.input.cells.begin(), large.input.cells.end(),
                     [](auto cell) { return cell <= 1; }), "binary cells only");
    const auto alive = static_cast<std::size_t>(std::count(large.input.cells.begin(),
                                                        large.input.cells.end(), 1));
    check(large.metadata.alive_count == alive, "observed alive count");
    check(large.metadata.observed_density == static_cast<double>(alive) / 4000000,
          "observed density fraction");
    check(large.metadata.width == 2000 && large.metadata.height == 2000
          && large.input.width == 2000 && large.input.height == 2000
          && large.metadata.seed == 42 && large.metadata.requested_density == 3000
          && large.metadata.version == "mt19937-row-major-rejection-v1", "metadata identity");

    for (auto config : {GenerationConfig{0,1,0,0}, {1,0,0,0}, {0,0,0,0},
                        {1,1,-1,0}, {1,1,10001,0}})
        rejects<std::invalid_argument>([&] { generateInput(config); }, "invalid parameters");
    const auto max = std::numeric_limits<std::size_t>::max();
    const auto signed_max = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    for (auto config : {GenerationConfig{max,1,0,0}, {max/2+1,2,0,0},
                        {max-2,1,0,0}, {1,signed_max,0,0},
                        {signed_max,1,0,0}, {100000,100000,0,0}})
        rejects<std::length_error>([&] { generateInput(config); }, "unsafe dimensions/budget");
    // Each individual vector fits ptrdiff_t, but their combined payload overflows size_t.
    constexpr int bits = std::numeric_limits<std::size_t>::digits;
    const auto wide = std::size_t{3} << (bits / 2 - 2);
    const auto tall = std::size_t{1} << (bits / 2 - 1);
    rejects<std::length_error>([&] { generateInput({wide,tall,0,0}, {max}); },
                               "combined payload addition overflow");
    rejects<std::length_error>([] { generateInput({1,1,0,0}, {18}); }, "payload budget boundary");
    check(generateInput({1,1,0,0}, {19}).input.cells.size() == 1,
          "exact payload budget: one input byte plus two 3x3 buffers");
    rejects<std::length_error>([] { generateInput({1,1,0,0}, {0}); }, "zero budget");

    for (auto config : {GenerationConfig{1,1,5000,0}, {1,17,3000,42},
                        {17,1,3000,42}, {127,65,3000,2026}}) {
        const auto generated = generateInput(config);
        const auto original = generated.input.cells;
        for (int threads : {1,2,4,8}) {
            omp_set_dynamic(0);
            omp_set_num_threads(threads);
            conway::Game sequential(generated.input), parallel(generated.input);
            parallel.setEngine(conway::Engine::OpenMP);
            check(sequential.width() == config.width && sequential.height() == config.height
                  && sequential.generation() == 0
                  && sequential.aliveCount() == generated.metadata.alive_count,
                  "Game accepts generated input");
            for (std::size_t row = 0; row < config.height; ++row)
                for (std::size_t col = 0; col < config.width; ++col)
                    check(sequential.isAlive(row,col)
                          == (original[row*config.width+col] != 0), "Game initial cells");
            for (int generation = 0; generation <= 10; ++generation) {
                check(sequential.generation() == parallel.generation()
                      && sequential.aliveCount() == parallel.aliveCount(), "engine counters");
                for (std::size_t row = 0; row < config.height; ++row)
                    for (std::size_t col = 0; col < config.width; ++col)
                        check(sequential.isAlive(row,col) == parallel.isAlive(row,col),
                              "generated input engine equivalence");
                if (generation < 10) { sequential.step(); parallel.step(); }
            }
        }
        check(generated.input.cells == original, "Game leaves generated input unchanged");
    }
    if (failures) { std::cerr << failures << " failures\n"; return 1; }
    std::cout << "PASS: generated inputs, golden sequences, safety, metadata and Game/OpenMP equivalence\n";
}

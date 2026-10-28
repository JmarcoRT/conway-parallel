#include "core/engines/cuda.hpp"
#include "core/engines/sequential.hpp"
#include "core/game.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace conway::core;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Error, class Action>
void throws(Action action) {
    try { action(); }
    catch (const Error&) { return; }
    throw std::runtime_error("Expected exception was not thrown");
}

void validation() {
    throws<std::invalid_argument>([] { CudaBackend gpu({0, 1, {}}); });
    throws<std::invalid_argument>([] { CudaBackend gpu({1, 0, {}}); });
    throws<std::invalid_argument>([] { CudaBackend gpu({2, 2, {1}}); });
    throws<std::invalid_argument>([] { CudaBackend gpu({1, 1, {2}}); });
    throws<std::length_error>([] { CudaBackend gpu({std::numeric_limits<std::size_t>::max(), 2, {}}); });
    throws<std::length_error>([] { CudaBackend gpu({std::numeric_limits<std::size_t>::max() / 4, 8, {}}); });
    throws<std::length_error>([] { CudaBackend gpu({std::numeric_limits<std::size_t>::max() / 3, 2, {}}); });
    throws<std::invalid_argument>([] { CudaBackend gpu({1, 1, {0, 1}}); });
    static_assert(!std::is_copy_constructible_v<CudaBackend>);
    static_assert(!std::is_copy_assignable_v<CudaBackend>);
    static_assert(std::is_nothrow_move_constructible_v<CudaBackend>);
    static_assert(std::is_nothrow_move_assignable_v<CudaBackend>);
    std::cout << "PASS input validation before runtime access; exclusive movable ownership\n";
}

GameState cpu_state(const InputData& input, std::size_t generation) {
    const auto stride = input.width + 2;
    GameState state{input.width, input.height, stride,
                    std::vector<std::uint8_t>(stride * (input.height + 2)),
                    std::vector<std::uint8_t>(stride * (input.height + 2)), generation, 0};
    for (std::size_t r = 0; r < input.height; ++r)
        for (std::size_t c = 0; c < input.width; ++c) {
            const auto value = input.cells[r * input.width + c];
            state.current[(r + 1) * stride + c + 1] = value;
            state.aliveCount += value;
        }
    return state;
}

void compare(const GameState& got, const GameState& expected, const conway::Game& game) {
    require(got.width == expected.width && got.height == expected.height && got.stride == expected.stride,
            "Snapshot dimensions/stride mismatch");
    require(got.current == expected.current && got.next == expected.next, "Full buffers/halos differ from CPU");
    require(got.generation == expected.generation && got.aliveCount == expected.aliveCount,
            "Snapshot generation/population mismatch");
    require(got.generation == game.generation() && got.aliveCount == game.aliveCount(), "Game counters mismatch");
    std::size_t alive = 0;
    for (std::size_t r = 0; r < got.height + 2; ++r)
        for (std::size_t c = 0; c < got.stride; ++c) {
            const auto index = r * got.stride + c;
            if (r == 0 || c == 0 || r == got.height + 1 || c == got.width + 1) {
                require(got.current[index] == 0 && got.next[index] == 0, "Nonzero dead-border halo");
            } else {
                require(got.current[index] <= 1, "Nonbinary output");
                require((got.current[index] != 0) == game.isAlive(r - 1, c - 1), "Game cell mismatch");
                alive += got.current[index];
            }
        }
    require(alive == got.aliveCount, "Population does not equal cell sum");
}

void equivalence(const InputData& input) {
    constexpr std::size_t start = 7;
    conway::Game game(input, start);
    auto expected = cpu_state(input, start);
    CudaBackend gpu(input, start);
    auto saved = gpu.snapshot();
    const auto savedCells = saved.current;
    for (int generation = 0; generation <= 12; ++generation) {
        compare(gpu.snapshot(), expected, game);
        if (generation != 12) { gpu.step(); step_sequential(expected); game.step(); }
    }
    require(saved.current == savedCells && saved.generation == start, "Old snapshot was changed by GPU steps");
    auto detached = gpu.snapshot();
    std::fill(detached.current.begin(), detached.current.end(), 0xff);
    std::fill(detached.next.begin(), detached.next.end(), 0xff);
    compare(gpu.snapshot(), expected, game);
    // CPU continuation from a detached snapshot uses the ordinary existing core.
    auto continued = gpu.snapshot();
    step_sequential(continued);
    gpu.step(); game.step();
    compare(gpu.snapshot(), continued, game);
}

InputData board(std::size_t w, std::size_t h, unsigned mode) {
    InputData input{w, h, std::vector<std::uint8_t>(w * h)};
    std::uint32_t random = 0x13579bdf;
    for (std::size_t r = 0; r < h; ++r)
        for (std::size_t c = 0; c < w; ++c) {
            random = random * 1664525u + 1013904223u;
            auto& value = input.cells[r * w + c];
            if (mode == 1) value = 1;
            if (mode == 2) value = (r + c) % 2;
            if (mode == 3) value = random >> 31;
            if (mode == 4) value = (r == 0 || c == 0 || r + 1 == h || c + 1 == w);
        }
    return input;
}

void patterns() {
    // Stable block, blinker, glider, toad, beacon; also placed at dead edges.
    const std::vector<std::vector<std::pair<unsigned, unsigned>>> patterns = {
        {{0,0},{0,1},{1,0},{1,1}}, {{0,0},{0,1},{0,2}},
        {{0,1},{1,2},{2,0},{2,1},{2,2}}, {{0,1},{0,2},{0,3},{1,0},{1,1},{1,2}},
        {{0,0},{0,1},{1,0},{1,1},{2,2},{2,3},{3,2},{3,3}}
    };
    for (const auto& pattern : patterns)
        for (const auto origin : {std::pair{0u,0u}, std::pair{7u,9u}, std::pair{15u,19u}}) {
            auto input = board(23, 19, 0);
            for (const auto [r,c] : pattern) input.cells[(origin.first+r)*input.width+origin.second+c] = 1;
            equivalence(input);
        }
}

void all_neighborhoods() {
    // 512 isolated 3x3 centers cover every center/8-neighbor combination.
    auto input = board(32 * 3, 16 * 3, 0);
    for (unsigned mask = 0; mask < 512; ++mask)
        for (unsigned bit = 0; bit < 9; ++bit)
            input.cells[((mask / 32) * 3 + bit / 3) * input.width + (mask % 32) * 3 + bit % 3] = (mask >> bit) & 1;
    CudaBackend gpu(input);
    gpu.step();
    const auto result = gpu.snapshot();
    for (unsigned mask = 0; mask < 512; ++mask) {
        const auto r = (mask / 32) * 3 + 2, c = (mask % 32) * 3 + 2;
        const auto center = (mask >> 4) & 1;
        unsigned neighbors = 0;
        for (unsigned bit = 0; bit < 9; ++bit) if (bit != 4) neighbors += (mask >> bit) & 1;
        const auto expected = neighbors == 3 || (center && neighbors == 2);
        require(result.current[r * result.stride + c] == expected, "Exhaustive B3/S23 oracle mismatch");
    }
    equivalence(input);
}

void lifetime_and_errors() {
    const auto input = board(17, 19, 3);
    auto expected = cpu_state(input, 0);
    step_sequential(expected);
    conway::Game game(input);
    game.step();
    GameState saved;
    {
        CudaBackend first(input), second(input);
        first.step();
        require(second.snapshot().generation == 0, "Backend instances share state");
        CudaBackend moved(std::move(first));
        throws<std::logic_error>([&] { first.step(); });
        throws<std::logic_error>([&] { (void)first.snapshot(); });
        second = std::move(moved);
        throws<std::logic_error>([&] { moved.step(); });
        saved = second.snapshot();
        compare(saved, expected, game);
        second = std::move(second);
        compare(second.snapshot(), expected, game);
        throws<std::logic_error>([&] { (void)moved.snapshot(); });
    }
    compare(saved, expected, game);
    for (int i = 0; i < 16; ++i) { CudaBackend temporary(input); temporary.step(); }
    compare(saved, expected, game);
    throws<std::invalid_argument>([&] { CudaBackend bad(input, 0, -1); });
    throws<std::invalid_argument>([&] { CudaBackend bad(input, 0, cuda_device_status().deviceCount); });
    CudaBackend maximum(input, std::numeric_limits<std::size_t>::max());
    conway::Game maximumGame(input, std::numeric_limits<std::size_t>::max());
    auto maximumState = cpu_state(input, std::numeric_limits<std::size_t>::max());
    maximum.step(); maximumGame.step(); step_sequential(maximumState);
    compare(maximum.snapshot(), maximumState, maximumGame);
    require(maximum.snapshot().generation == 0, "Generation does not wrap like Game's size_t counter");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--validation-only") { validation(); return 0; }
        const auto status = cuda_device_status();
        std::cout << status.diagnostic << " (code=" << status.errorCode << ", devices=" << status.deviceCount << ")\n";
        if (argc == 2 && std::string_view(argv[1]) == "--no-device") {
            require(status.availability == CudaAvailability::NoDevice, "Expected explicit NoDevice status");
            throws<CudaError>([] { CudaBackend unavailable({1, 1, {1}}); });
            validation();
            return 0;
        }
        if (status.availability == CudaAvailability::NoDevice) return 77;
        require(status.availability == CudaAvailability::Available, "CUDA driver/runtime failure (not a skipped GPU test)");
        for (const auto [w,h] : {std::pair{1u,1u}, {1u,257u}, {257u,1u}, {2u,2u}, {3u,3u},
                               {17u,19u}, {31u,33u}, {255u,2u}, {256u,3u}, {257u,5u}, {513u,517u}})
            for (unsigned mode = 0; mode < 5; ++mode) equivalence(board(w,h,mode));
        patterns();
        all_neighborhoods();
        lifetime_and_errors();
        std::cout << "PASS exact Game/CPU/GPU equivalence, full buffers/halos, population, generations,\n"
                     "512 rule neighborhoods, resident multi-step evolution, independent snapshots, moves and errors\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

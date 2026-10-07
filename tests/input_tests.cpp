#include "core/game.hpp"
#include "input/input.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Cells = std::vector<std::uint8_t>;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Each run owns a separate directory; fixtures and repository files are never overwritten.
struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = fs::temp_directory_path() /
                ("conway-input-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("No se pudo crear el directorio temporal");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path write(const std::string& name, const std::string& content) const {
        auto file = path / name;
        std::ofstream out(file, std::ios::binary);
        out << content;
        out.close();
        require(static_cast<bool>(out), "No se pudo escribir el archivo temporal");
        return file;
    }
};

void expectInput(const InputData& data, std::size_t height, std::size_t width,
                 const std::string& flat) {
    require(data.height == height && data.width == width, "Dimensiones incorrectas");
    Cells expected;
    for (char c : flat) expected.push_back(static_cast<std::uint8_t>(c - '0'));
    require(data.cells == expected, "Contenido u orden de las celulas incorrecto");
}

void expectGame(const conway::Game& game, const std::string& flat) {
    require(flat.size() == game.width() * game.height(), "Oraculo de prueba incorrecto");
    std::size_t alive = 0;
    for (std::size_t row = 0; row < game.height(); ++row) {
        for (std::size_t col = 0; col < game.width(); ++col) {
            bool expected = flat[row * game.width() + col] == '1';
            require(game.isAlive(row, col) == expected, "Estado del patron incorrecto");
            alive += expected;
        }
    }
    require(game.aliveCount() == alive, "Conteo de celulas incorrecto");
}

void expectRejected(const fs::path& file, const std::string& diagnostic) {
    try {
        (void)loadInput(file.string());
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(diagnostic) != std::string::npos,
                "Diagnostico inesperado: " + std::string(error.what()));
        return;
    } catch (const std::exception& error) {
        throw std::runtime_error("Excepcion distinta de runtime_error: " + std::string(error.what()));
    }
    throw std::runtime_error("Se acepto una entrada invalida");
}

struct Suite {
    int passed = 0;
    int failed = 0;
    void test(const std::string& name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
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

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3 ||
            (argc == 3 && std::string(argv[2]) != "--dimension-regressions")) {
            std::cerr << "Uso: input_tests <raiz-proyecto> [--dimension-regressions]\n";
            return 2;
        }
        const fs::path root = fs::absolute(argv[1]);
        const auto fixtures = root / "tests/fixtures/input";
        const auto invalid = fixtures / "invalid";
        TemporaryDirectory temporary;
        Suite suite;
        if (argc == 3) {
            // Pending fixes are deliberately reproducible and return failure, never a false PASS.
            for (const auto* name : {"negative_height.txt", "negative_width.txt"}) {
                suite.test(name, [&] { expectRejected(invalid / name, "dimensiones"); });
            }
            suite.test("dimension_product_overflow", [&] {
                const auto height = std::numeric_limits<std::size_t>::max() / 2 + 1;
                auto file = temporary.write("overflow.txt", std::to_string(height) + " 2\n");
                expectRejected(file, "dimensiones");
            });
            return suite.result();
        }

        const std::string block = "0000" "0110" "0110" "0000";
        const std::string blinker = "00000" "00100" "00100" "00100" "00000";
        const std::string glider = "0000000" "0010000" "0001000" "0111000" "0000000" "0000000";
        struct ValidCase {
            std::string path;
            std::size_t height;
            std::size_t width;
            std::string cells;
        };
        for (const auto& item : std::vector<ValidCase>{
                 {"patterns/block.txt", 4, 4, block},
                 {"patterns/blinker.txt", 5, 5, blinker},
                 {"patterns/glider.txt", 6, 7, glider},
                 {"patterns/test.txt", 5, 5, blinker},
                 {"tests/fixtures/input/valid/single_dead.txt", 1, 1, "0"},
                 {"tests/fixtures/input/valid/single_alive.txt", 1, 1, "1"},
                 {"tests/fixtures/input/valid/rectangle.txt", 2, 4, "01011010"}}) {
            suite.test(item.path, [&] {
                const auto input = loadInput((root / item.path).string());
                expectInput(input, item.height, item.width, item.cells);
                conway::Game game(input);
                require(game.generation() == 0, "Generacion inicial incorrecta");
                expectGame(game, item.cells);
            });
        }
        for (const auto& [name, content] : std::vector<std::pair<std::string, std::string>>{
                 {"crlf", "2 4\r\n0101\r\n1010\r\n"},
                 {"trailing_whitespace", "2 4\n0101  \n1010\t\n\n"},
                 {"no_final_newline", "2 4\n0101\n1010"}}) {
            suite.test(name, [&] {
                auto path = temporary.write(name + ".txt", content);
                expectInput(loadInput(path.string()), 2, 4, "01011010");
            });
        }
        for (const auto& [name, diagnostic] : std::vector<std::pair<std::string, std::string>>{
                 {"missing_dimensions.txt", "No se pudieron leer las dimensiones"},
                 {"nonnumeric_dimensions.txt", "No se pudieron leer las dimensiones"},
                 {"dimensions_out_of_range.txt", "No se pudieron leer las dimensiones"},
                 {"zero_height.txt", "Las dimensiones deben ser mayores que cero"},
                 {"zero_width.txt", "Las dimensiones deben ser mayores que cero"},
                 {"missing_rows.txt", "Faltan filas"},
                 {"extra_rows.txt", "datos adicionales"},
                 {"short_row.txt", "no tiene el ancho esperado"},
                 {"long_row.txt", "no tiene el ancho esperado"},
                 {"spaced_cells.txt", "no tiene el ancho esperado"},
                 {"invalid_character.txt", "solo puede contener 0 y 1"},
                 {"nonbinary_digit.txt", "solo puede contener 0 y 1"},
                 {"trailing_data.txt", "datos adicionales"}}) {
            suite.test(name, [&] { expectRejected(invalid / name, diagnostic); });
        }
        suite.test("empty_file", [&] {
            expectRejected(temporary.write("empty.txt", ""), "No se pudieron leer las dimensiones");
        });
        suite.test("missing_file", [&] {
            expectRejected(temporary.path / "does-not-exist.txt", "No se pudo abrir el archivo");
        });
        suite.test("change_file_A_B_A", [&] {
            auto active = loadInput((root / "patterns/block.txt").string());
            const auto original = active;
            active = loadInput((root / "patterns/glider.txt").string());
            expectInput(active, 6, 7, glider);
            conway::Game game(active);
            expectGame(game, glider);
            expectInput(original, 4, 4, block);
            active = loadInput((root / "patterns/block.txt").string());
            expectInput(active, 4, 4, block);
        });
        for (const auto& path : {invalid / "invalid_character.txt", temporary.path / "missing.txt"}) {
            suite.test("failed_change_" + path.filename().string(), [&] {
                auto active = loadInput((root / "patterns/block.txt").string());
                bool rejected = false;
                try { active = loadInput(path.string()); }
                catch (const std::runtime_error&) { rejected = true; }
                require(rejected, "La carga invalida no fue rechazada");
                expectInput(active, 4, 4, block);
                active = loadInput((root / "patterns/glider.txt").string());
                expectInput(active, 6, 7, glider);
            });
        }
        suite.test("block_stable", [&] {
            conway::Game game(loadInput((root / "patterns/block.txt").string()));
            game.step(); expectGame(game, block);
        });
        suite.test("blinker_period_2", [&] {
            conway::Game game(loadInput((root / "patterns/blinker.txt").string()));
            game.step();
            expectGame(game, "00000" "00000" "01110" "00000" "00000");
            game.step(); expectGame(game, blinker);
            require(game.generation() == 2, "Generacion incorrecta");
        });
        suite.test("glider_moves_after_4_steps", [&] {
            const auto input = loadInput((root / "patterns/glider.txt").string());
            conway::Game game(input);
            for (int i = 0; i < 4; ++i) game.step();
            expectGame(game, "0000000" "0000000" "0001000" "0000100" "0011100" "0000000");
            expectInput(input, 6, 7, glider);
            conway::Game fresh(input);
            expectGame(fresh, glider);
        });
        return suite.result();
    } catch (const std::exception& error) {
        std::cerr << "Error de preparacion: " << error.what() << '\n';
        return 2;
    }
}

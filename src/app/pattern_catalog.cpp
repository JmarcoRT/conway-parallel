#include "app/pattern_catalog.hpp"

#include "input/input.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <system_error>

namespace conway::app {

namespace {

struct PatternMetadata {
    std::string type;
    std::string description;
    std::optional<std::size_t> period;
};

PatternMetadata metadataFor(const std::string& name) {
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if (normalized == "block") {
            return {"Estructura estable", "Permanece sin cambios", 1};
    }
    if (normalized == "blinker") {
        return {"Oscilador", "Alterna entre dos fases", 2};
    }
    if (normalized == "toad" || normalized == "beacon") {
        return {"Oscilador", "Alterna entre dos fases", 2};
    }
    if (normalized == "glider") {
        return {"Planeador", "Se desplaza en diagonal", 4};
    }
    if (normalized == "pulsar") {
        return {"Oscilador", "Oscilador de periodo largo", 3};
    }
    if (normalized == "lightweight_spaceship") {
        return {"Nave espacial", "Nave ortogonal de periodo 4", 4};
    }
    if (normalized == "middleweight_spaceship") {
        return {"Nave espacial", "Nave ortogonal de periodo 4", 4};
    }
    if (normalized == "heavyweight_spaceship") {
        return {"Nave espacial", "Nave ortogonal de periodo 4", 4};
    }
    if (normalized == "loafer") {
        return {"Nave espacial", "Nave ortogonal de periodo 7", 7};
    }
    if (normalized == "copperhead") {
        return {"Nave espacial", "Nave ortogonal de periodo 10", 10};
    }
    if (normalized == "weekender") {
        return {"Nave espacial", "Nave ortogonal de periodo 7", 7};
    }
    if (normalized == "spider") {
        return {"Nave espacial", "Nave ortogonal de periodo 5", 5};
    }
    if (normalized == "pentadecathlon") {
        return {"Oscilador", "Oscilador de periodo 15", 15};
    }
    if (normalized == "koks_galaxy") {
        return {"Oscilador", "Oscilador de periodo 8", 8};
    }
    if (normalized == "queen_bee_shuttle") {
        return {"Lanzadera", "Lanzadera de periodo 30", 30};
    }
    if (normalized == "b29") {
        return {"Nave espacial", "Nave diagonal de periodo 4", 4};
    }
    if (normalized == "dragon") {
        return {"Nave espacial", "Nave ortogonal de periodo 6", 6};
    }
    if (normalized == "sir_robin") {
        return {"Nave espacial", "Nave oblicua de periodo 6", 6};
    }
    if (normalized == "pufferfish") {
        return {"Puffer", "Puffer casi natural de velocidad c/2", std::nullopt};
    }
    if (normalized == "max") {
        return {"Rellenador espacial", "Crece hasta llenar el tablero", std::nullopt};
    }
    if (normalized == "acorn") {
        return {"Matusalén", "Evolución caótica de larga vida", std::nullopt};
    }
    if (normalized == "heart") {
        return {"Figura", "Corazón de periodo 5", 5};
    }
    if (normalized == "smiley") {
        return {"Figura", "Carita de periodo 8", 8};
    }
    if (normalized == "star") {
        return {"Figura", "Estrella de periodo 3", 3};
    }
    if (normalized == "cross") {
        return {"Figura", "Cruz de periodo 3", 3};
    }
    if (normalized == "diamond_ring") {
        return {"Figura", "Anillo de diamante de periodo 3", 3};
    }
    if (normalized == "clock") {
        return {"Oscilador", "Reloj de periodo 2", 2};
    }
    if (normalized == "figure_eight") {
        return {"Oscilador", "Ocho de periodo 8", 8};
    }
    if (normalized == "tumbler") {
        return {"Oscilador", "Tumbler de periodo 14", 14};
    }
    if (normalized == "pinwheel") {
        return {"Oscilador", "Molino de periodo 4", 4};
    }
    if (normalized == "spiral") {
        return {"Estructura estable", "Espiral de 20 células", 1};
    }
    if (normalized == "swan") {
        return {"Nave espacial", "Nave diagonal de periodo 4", 4};
    }
    if (normalized == "turtle") {
        return {"Nave espacial", "Nave ortogonal de periodo 3", 3};
    }
    return {};
}

} // namespace

PatternCatalog discoverPatterns(const std::filesystem::path& directory) {
    PatternCatalog catalog;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        catalog.error = "No se pudo leer el catálogo de patrones: " + error.message();
        return catalog;
    }

    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            catalog.error = "Error al recorrer el catálogo de patrones: " + error.message();
            return catalog;
        }
        std::error_code entryError;
        const bool regularFile = iterator->is_regular_file(entryError);
        if (entryError) {
            catalog.error = "No se pudo consultar un archivo del catálogo: " +
                entryError.message();
            return catalog;
        }
        if (!regularFile) {
            continue;
        }
        std::string extension = iterator->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (extension != ".txt") {
            continue;
        }

        PatternEntry entry;
        entry.name = iterator->path().stem().string();
        entry.path = (directory / iterator->path().filename()).generic_string();
        const PatternMetadata metadata = metadataFor(entry.name);
        entry.type = metadata.type;
        entry.description = metadata.description;
        entry.period = metadata.period;
        try {
            const InputData input = loadInput(entry.path);
            entry.width = input.width;
            entry.height = input.height;
            entry.aliveCells = static_cast<std::size_t>(
                std::count(input.cells.begin(), input.cells.end(), std::uint8_t{1}));
        } catch (const std::exception& exception) {
            entry.error = exception.what();
        }
        catalog.entries.push_back(std::move(entry));
    }
    if (error) {
        catalog.error = "Error al recorrer el catálogo de patrones: " + error.message();
        return catalog;
    }

    std::sort(catalog.entries.begin(), catalog.entries.end(),
        [](const PatternEntry& lhs, const PatternEntry& rhs) {
            return lhs.name < rhs.name;
        });
    return catalog;
}

} // namespace conway::app

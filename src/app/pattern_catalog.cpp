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

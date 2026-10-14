#include "app/pattern_preview.hpp"

#include "input/input.hpp"

#include <exception>

namespace conway::app {

PatternPreview buildPatternPreview(const PatternEntry& entry) {
    PatternPreview preview;
    if (!entry.valid()) {
        preview.error = "No se puede previsualizar " + entry.name + ": " +
            entry.error;
        return preview;
    }

    try {
        const InputData input = loadInput(entry.path);
        preview.current.emplace(input);
        preview.next.emplace(*preview.current);
        preview.next->step();
    } catch (const std::exception& error) {
        preview.current.reset();
        preview.next.reset();
        preview.error = "No se pudo generar la vista previa de " + entry.name +
            ": " + error.what();
    }
    return preview;
}

} // namespace conway::app

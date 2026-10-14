#pragma once

#include "app/pattern_catalog.hpp"
#include "core/game.hpp"

#include <optional>
#include <string>

namespace conway::app {

struct PatternPreview {
    std::optional<Game> current;
    std::optional<Game> next;
    std::string error;
};

PatternPreview buildPatternPreview(const PatternEntry& entry);

} // namespace conway::app

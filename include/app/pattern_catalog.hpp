#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace conway::app {

struct PatternEntry {
    std::string name;
    std::string path;
    std::string type;
    std::string description;
    std::optional<std::size_t> period;
    std::size_t width{0};
    std::size_t height{0};
    std::size_t aliveCells{0};
    std::string error;

    bool valid() const noexcept { return error.empty(); }
};

struct PatternCatalog {
    std::vector<PatternEntry> entries;
    std::string error;
};

PatternCatalog discoverPatterns(const std::filesystem::path& directory);

} // namespace conway::app

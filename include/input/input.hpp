#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct InputData {
    std::size_t width;
    std::size_t height;
    std::vector<std::uint8_t> cells;
};

InputData loadInput(const std::string& path);
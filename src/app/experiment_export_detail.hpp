#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace conway::app::export_detail {
inline std::string number(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    char buffer[128];
    const auto [end,error] = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (error != std::errc{}) throw std::runtime_error("Cannot format experimental number");
    return {buffer,end};
}
template<class Integer> std::string integer(Integer value) {
    char buffer[64];
    const auto [end,error] = std::to_chars(buffer,buffer + sizeof(buffer),value);
    if (error != std::errc{}) throw std::runtime_error("Cannot format experimental integer");
    return {buffer,end};
}
inline std::string fingerprint(std::uint64_t value) {
    char buffer[16];
    const auto [end,error] = std::to_chars(buffer,buffer + sizeof(buffer),value,16);
    if (error != std::errc{}) throw std::runtime_error("Cannot format fingerprint");
    return std::string(16 - static_cast<std::size_t>(end-buffer),'0') + std::string(buffer,end);
}
inline std::string configurationId(std::optional<int> threads) {
    return threads ? "openmp:" + integer(*threads) : "sequential";
}
inline void validateUtf8(std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index++]);
        if (lead == 0) throw std::invalid_argument("CSV fields cannot contain NUL");
        if (lead < 0x80) continue;
        unsigned remaining, codepoint, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { remaining=1; codepoint=lead&0x1f; minimum=0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { remaining=2; codepoint=lead&0x0f; minimum=0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { remaining=3; codepoint=lead&0x07; minimum=0x10000; }
        else throw std::invalid_argument("CSV field is not UTF-8");
        if (text.size()-index < remaining) throw std::invalid_argument("Truncated UTF-8 field");
        while (remaining--) {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte&0xc0) != 0x80) throw std::invalid_argument("Invalid UTF-8 continuation");
            codepoint = (codepoint<<6) | (byte&0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
            throw std::invalid_argument("Invalid UTF-8 codepoint");
    }
}
} // namespace conway::app::export_detail

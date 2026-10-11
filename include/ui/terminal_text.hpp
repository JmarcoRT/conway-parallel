#pragma once

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <string>
#include <string_view>
#include <wchar.h>

namespace conway::ui::terminal_text {

inline int codepointWidth(std::string_view text, std::size_t offset) {
    std::mbstate_t state{};
    wchar_t character = L'\0';
    const std::size_t bytes = std::mbrtowc(
        &character, text.data() + offset, text.size() - offset, &state);
    if (bytes == static_cast<std::size_t>(-1) ||
        bytes == static_cast<std::size_t>(-2) || bytes == 0) {
        return 1;
    }
    const int width = ::wcwidth(character);
    return width < 0 ? 1 : width;
}

inline std::size_t visibleWidth(std::string_view text) {
    std::size_t width = 0;
    std::size_t offset = 0;
    bool escape = false;
    bool csi = false;
    while (offset < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[offset]);
        if (escape) {
            if (byte == '[') {
                csi = true;
            } else if (!csi || (byte >= '@' && byte <= '~')) {
                escape = false;
                csi = false;
            }
            ++offset;
        } else if (byte == 0x1b) {
            escape = true;
            ++offset;
        } else {
            const std::size_t before = offset;
            width += static_cast<std::size_t>(codepointWidth(text, offset));
            std::mbstate_t state{};
            wchar_t character = L'\0';
            const std::size_t bytes = std::mbrtowc(
                &character, text.data() + offset, text.size() - offset, &state);
            offset += bytes == static_cast<std::size_t>(-1) ||
                bytes == static_cast<std::size_t>(-2) || bytes == 0
                ? 1
                : bytes;
            if (offset <= before) {
                ++offset;
            }
        }
    }
    return width;
}

inline std::string truncate(std::string_view text, std::size_t maximumWidth) {
    if (visibleWidth(text) <= maximumWidth) {
        return std::string(text);
    }
    if (maximumWidth == 0) {
        return {};
    }

    const std::size_t contentLimit = maximumWidth - 1;
    std::string output;
    std::size_t width = 0;
    std::size_t offset = 0;
    bool escape = false;
    bool csi = false;
    while (offset < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[offset]);
        if (escape) {
            output += static_cast<char>(byte);
            if (byte == '[') {
                csi = true;
            } else if (!csi || (byte >= '@' && byte <= '~')) {
                escape = false;
                csi = false;
            }
            ++offset;
            continue;
        }
        if (byte == 0x1b) {
            escape = true;
            output += static_cast<char>(byte);
            ++offset;
            continue;
        }

        const int characterWidth = codepointWidth(text, offset);
        if (width + static_cast<std::size_t>(characterWidth) > contentLimit) {
            break;
        }
        std::mbstate_t state{};
        wchar_t character = L'\0';
        const std::size_t bytes = std::mbrtowc(
            &character, text.data() + offset, text.size() - offset, &state);
        const std::size_t length = bytes == static_cast<std::size_t>(-1) ||
            bytes == static_cast<std::size_t>(-2) || bytes == 0
            ? 1
            : bytes;
        output.append(text.substr(offset, length));
        offset += length;
        width += static_cast<std::size_t>(characterWidth);
    }
    output += "…";
    if (text.find("\033[") != std::string_view::npos) {
        output += "\033[0m";
    }
    return output;
}

inline std::string fit(std::string_view text, std::size_t width, bool centered = false) {
    const std::string clipped = truncate(text, width);
    const std::size_t used = visibleWidth(clipped);
    const std::size_t padding = width > used ? width - used : 0;
    const std::size_t left = centered ? padding / 2 : 0;
    return std::string(left, ' ') + clipped + std::string(padding - left, ' ');
}

} // namespace conway::ui::terminal_text

#pragma once

#include <optional>
#include <string_view>

namespace conway::app {

inline constexpr int kMaxOpenMPThreads = 64;

int availableLogicalProcessors() noexcept;
int defaultOpenMPThreadCount() noexcept;
std::optional<int> parseOpenMPThreadCount(std::string_view text) noexcept;

} // namespace conway::app

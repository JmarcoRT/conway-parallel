#include "app/thread_config.hpp"

#include <omp.h>

#include <algorithm>
#include <charconv>

namespace conway::app {

int availableLogicalProcessors() noexcept {
    return std::max(1, omp_get_num_procs());
}

int defaultOpenMPThreadCount() noexcept {
    return std::clamp(omp_get_max_threads(), 1, 8);
}

std::optional<int> parseOpenMPThreadCount(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    int count = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [parsedEnd, error] = std::from_chars(begin, end, count);
    if (error != std::errc{} || parsedEnd != end ||
        count < 1 || count > kMaxOpenMPThreads) {
        return std::nullopt;
    }
    return count;
}

} // namespace conway::app

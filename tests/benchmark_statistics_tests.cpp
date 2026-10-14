#include "app/benchmark.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* label) {
        if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
    };
    using conway::app::calculateBenchmarkStatistics;
    const std::vector<double> odd{9.0, 1.0, 5.0};
    const auto original = odd;
    const auto a = calculateBenchmarkStatistics(odd);
    check(a && a->minimumMs == 1 && a->maximumMs == 9 && a->medianMs == 5
          && a->meanMs == 5, "odd median, min, max, mean");
    check(a && std::abs(a->populationStddevMs - std::sqrt(32.0 / 3)) < 1e-12,
          "population standard deviation");
    check(odd == original, "statistics preserve input order");
    const std::vector<double> even{8, 2, 4, 6};
    const auto b = calculateBenchmarkStatistics(even);
    check(b && b->medianMs == 5 && b->meanMs == 5
          && std::abs(b->populationStddevMs - std::sqrt(5.0)) < 1e-12, "even samples");
    for (const auto& values : {std::vector<double>{7,7,7}, std::vector<double>{0,0},
                               std::vector<double>{3}}) {
        const auto stats = calculateBenchmarkStatistics(values);
        check(stats && stats->minimumMs == values[0] && stats->maximumMs == values[0]
              && stats->meanMs == values[0] && stats->medianMs == values[0]
              && stats->populationStddevMs == 0, "equal, zero and singleton samples");
    }
    check(!calculateBenchmarkStatistics({}), "empty data is explicitly unavailable");
    const std::vector<double> fractional{0, 0.125, 0.375};
    const auto fraction = calculateBenchmarkStatistics(fractional);
    check(fraction && fraction->medianMs == 0.125
          && std::abs(fraction->meanMs - 1.0/6) < 1e-15, "unrounded fractional times and zero");
    const double max = std::numeric_limits<double>::max();
    const std::vector<double> extreme{max,max};
    const auto large = calculateBenchmarkStatistics(extreme);
    check(large && large->medianMs == max && large->meanMs == max
          && large->populationStddevMs == 0, "finite extreme values do not overflow");
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try { const std::vector<double> values{0,invalid,1}; calculateBenchmarkStatistics(values); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "invalid time rejected explicitly");
    }
    // Existing aggregate initialization and default construction remain valid.
    const conway::app::BenchmarkResult legacy{1,2,0.5,0.25,2,2,2,true,true,8,3,true};
    check(legacy.sequentialMedianMs == 1 && legacy.equivalent && legacy.samples.empty()
          && !legacy.sequentialStatistics && !legacy.openMPStatistics, "legacy aggregate compatibility");
    std::cout << (failures ? "FAIL" : "PASS") << ": benchmark descriptive statistics\n";
    return failures ? 1 : 0;
}

#pragma once

#include "app/experiment_metadata.hpp"
#include <filesystem>
#include <ostream>

namespace conway::app {

struct CsvExportPaths {
    std::filesystem::path samples;
    std::filesystem::path summary;
    std::filesystem::path metadata;
};

// CSV v1 for legacy local results; v2 for result.study. UTF-8, comma separator,
// CRLF records and round-trip double precision.
// Context is combined with the supplied result; no experiment/statistics rerun.
void writeExperimentCsv(const ExperimentResult& result, const ExperimentContext& context,
                        std::ostream& samples, std::ostream& summary, std::ostream& metadata);
void writeExperimentCsv(const ScalingResult& result, const ExperimentContext& context,
                        std::ostream& samples, std::ostream& summary, std::ostream& metadata);

// Creates one NEW directory, with an existing parent. Existing destinations are
// always rejected. Partial files are removed best-effort after writing failures.
CsvExportPaths exportExperimentCsv(const ExperimentResult& result, const ExperimentContext& context,
                                   const std::filesystem::path& directory);
CsvExportPaths exportExperimentCsv(const ScalingResult& result, const ExperimentContext& context,
                                   const std::filesystem::path& directory);

} // namespace conway::app

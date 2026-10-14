#pragma once

#include "app/scaling_study.hpp"

namespace conway::app {

inline constexpr std::string_view kExperimentalProtocolVersion = "conway-experimental-v1";

enum class MetadataSource { RuntimeObserved, BuildCaptured, Result, UserProvided, Unavailable };
struct MetadataField {
    std::string key;
    std::optional<std::string> value;
    MetadataSource source;
    std::string detail; // Origin, scope, or reason for unavailable information.
};
struct MetadataOptions {
    std::string studyId; // Caller assigns an identifier; not used as a filesystem path.
    std::optional<std::string> patternName; // InputData itself does not retain provenance.
    std::vector<std::pair<std::string,std::string>> userFields; // Exported under user.*.
};
struct ExperimentContext {
    std::string studyId;
    std::optional<std::string> patternName;
    std::vector<MetadataField> fields;
};
struct ExperimentMetadata {
    std::string studyId;
    std::string experimentType;
    std::vector<MetadataField> fields;
};

// Call immediately before running the experiment. Timestamp denotes this context
// snapshot, not an internally instrumented start time. Never invokes external tools.
ExperimentContext captureExperimentContext(const MetadataOptions& options);
ExperimentMetadata describeExperiment(const ExperimentContext& context, const ExperimentResult& result);
ExperimentMetadata describeExperiment(const ExperimentContext& context, const ScalingResult& result);
std::string_view metadataSourceName(MetadataSource source);

} // namespace conway::app

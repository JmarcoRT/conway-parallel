#include "app/experiment_metadata.hpp"
#include "experiment_export_detail.hpp"

#include <omp.h>
#include <chrono>
#include <cstdlib>
#include <set>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif
#ifdef CONWAY_CAPTURED_BUILD_INFO
#include "experiment_build_info.hpp"
#endif

namespace conway::app {
namespace {
using export_detail::integer;
using export_detail::number;
void add(std::vector<MetadataField>& fields, std::string key, std::string value,
         MetadataSource source, std::string detail = {}) {
    fields.push_back({std::move(key),std::move(value),source,std::move(detail)});
}
void unavailable(std::vector<MetadataField>& fields, std::string key, std::string reason) {
    fields.push_back({std::move(key),std::nullopt,MetadataSource::Unavailable,std::move(reason)});
}
std::string timestamp() {
    using namespace std::chrono;
    const auto now = floor<seconds>(system_clock::now());
    const auto day = floor<days>(now);
    const year_month_day date{day};
    const hh_mm_ss time{now-day};
    const auto pad = [](auto value) { auto text=integer(value); return text.size()<2 ? "0"+text : text; };
    return integer(static_cast<int>(date.year())) + "-" + pad(static_cast<unsigned>(date.month()))
        + "-" + pad(static_cast<unsigned>(date.day())) + "T" + pad(time.hours().count())
        + ":" + pad(time.minutes().count()) + ":" + pad(time.seconds().count()) + "Z";
}
void buildFields(std::vector<MetadataField>& fields) {
#ifdef CONWAY_CAPTURED_BUILD_INFO
    const auto captured = [&](const char* key, std::string_view value) {
        if (value.empty()) unavailable(fields,key,"Not available in controlled build snapshot");
        else add(fields,key,std::string(value),MetadataSource::BuildCaptured,
                 "CMake build-step snapshot; not a live query at execution time");
    };
    captured("compiler.id",build_info::compilerId);
    captured("compiler.version",build_info::compilerVersion);
    captured("compiler.path",build_info::compilerPath);
    captured("build.type",build_info::buildType);
    captured("build.snapshot_captured_at_utc",build_info::capturedAt);
    captured("build.git_commit",build_info::gitCommit);
    captured("build.git_state",build_info::gitState);
    captured("build.command.sequential",build_info::command_sequential);
    captured("build.command.openmp",build_info::command_openmp);
    captured("build.command.benchmark",build_info::command_benchmark);
    captured("build.command.comparison",build_info::command_comparison);
    captured("build.command.scaling",build_info::command_scaling);
#else
#ifdef __VERSION__
    add(fields,"compiler.version",__VERSION__,MetadataSource::BuildCaptured,"Compiler predefined macro for metadata translation unit");
#else
    unavailable(fields,"compiler.version","Compiler version macro unavailable");
#endif
    for (const char* key : {"compiler.id","compiler.path","build.type","build.snapshot_captured_at_utc",
                           "build.git_commit","build.git_state","build.command.sequential","build.command.openmp",
                           "build.command.benchmark","build.command.comparison","build.command.scaling"})
        unavailable(fields,key,"Not built through the controlled CMake metadata target");
#endif
}
ExperimentMetadata base(const ExperimentContext& context, const char* kind,
                        const ExperimentInputIdentity& input) {
    if (context.studyId.empty()) throw std::invalid_argument("Study identifier must not be empty");
    export_detail::validateUtf8(context.studyId);
    ExperimentMetadata metadata{context.studyId,kind,context.fields};
    auto& fields=metadata.fields;
    add(fields,"csv.schema_version","1",MetadataSource::Result);
    add(fields,"csv.files","samples.csv;summary.csv;metadata.csv",MetadataSource::Result);
    add(fields,"study.id",context.studyId,MetadataSource::UserProvided);
    add(fields,"experiment.type",kind,MetadataSource::Result);
    add(fields,"protocol.version",std::string(kExperimentalProtocolVersion),MetadataSource::Result,
        "Methodology contract used by comparison/scaling APIs; CSV schema is separately versioned");
    add(fields,"input.width",integer(input.width),MetadataSource::Result);
    add(fields,"input.height",integer(input.height),MetadataSource::Result);
    add(fields,"input.initial_alive_count",integer(input.aliveCount),MetadataSource::Result);
    add(fields,"input.fingerprint",export_detail::fingerprint(input.fingerprint),MetadataSource::Result);
    add(fields,"input.fingerprint_algorithm","fnv1a64-dimensions-le64-row-major-v1",MetadataSource::Result);
    if (input.generated) {
        add(fields,"input.origin","generator",MetadataSource::Result);
        add(fields,"input.seed",integer(input.generated->seed),MetadataSource::Result);
        add(fields,"input.requested_density_per_10000",integer(input.generated->requestedDensity),MetadataSource::Result);
        add(fields,"input.observed_density_fraction",number(input.generated->observedDensity),MetadataSource::Result);
        add(fields,"input.generator_version",input.generated->generatorVersion,MetadataSource::Result);
        unavailable(fields,"input.pattern_name","Not applicable to generated input");
        if (context.patternName) throw std::invalid_argument("Generated input cannot be labelled as a loaded pattern");
    } else {
        if (context.patternName) {
            add(fields,"input.origin","pattern",MetadataSource::UserProvided,"Caller-supplied provenance; InputData does not retain a filename");
            add(fields,"input.pattern_name",*context.patternName,MetadataSource::UserProvided);
        } else {
            unavailable(fields,"input.origin","InputData provenance not retained; caller did not supply a pattern name");
            unavailable(fields,"input.pattern_name","Not supplied by caller");
        }
        for (const char* key : {"input.seed","input.requested_density_per_10000","input.observed_density_fraction","input.generator_version"})
            unavailable(fields,key,"Not available for an existing InputData");
    }
    add(fields,"execution.sample_order_location","samples.csv:execution_order,round,position,configuration_id",MetadataSource::Result);
    add(fields,"units.elapsed_time","milliseconds_per_generation_batch",MetadataSource::Result);
    add(fields,"statistics.stddev_definition","population (divisor N)",MetadataSource::Result);
    return metadata;
}
void executionFields(ExperimentMetadata& metadata, int generations, int warmups, int repetitions,
                     std::size_t completed, std::size_t samples, std::string threads, std::string order) {
    auto& fields=metadata.fields;
    add(fields,"execution.generations",integer(generations),MetadataSource::Result);
    add(fields,"execution.warmups_per_configuration",integer(warmups),MetadataSource::Result);
    add(fields,"execution.repetitions",integer(repetitions),MetadataSource::Result);
    add(fields,"execution.completed_warmup_runs",integer(completed),MetadataSource::Result);
    add(fields,"execution.measured_samples",integer(samples),MetadataSource::Result);
    add(fields,"execution.requested_threads",std::move(threads),MetadataSource::Result,"Semicolon-separated list, preserving requested order");
    add(fields,"execution.order_policy",std::move(order),MetadataSource::Result);
}
} // namespace

std::string_view metadataSourceName(MetadataSource source) {
    switch(source) {
        case MetadataSource::RuntimeObserved: return "runtime_observed";
        case MetadataSource::BuildCaptured: return "build_captured";
        case MetadataSource::Result: return "result";
        case MetadataSource::UserProvided: return "user";
        case MetadataSource::Unavailable: return "unavailable";
    }
    throw std::invalid_argument("Unknown metadata source");
}
ExperimentContext captureExperimentContext(const MetadataOptions& options) {
    if (options.studyId.empty()) throw std::invalid_argument("Study identifier must not be empty");
    export_detail::validateUtf8(options.studyId);
    ExperimentContext context{options.studyId,options.patternName,{}};
    auto& fields=context.fields;
    add(fields,"execution.context_captured_at_utc",timestamp(),MetadataSource::RuntimeObserved,
        "Snapshot timestamp; capture immediately before execution, not during export");
    unavailable(fields,"execution.internal_start_at_utc","Existing runners do not instrument wall-clock start; snapshot timestamp is separate");
#if defined(__unix__) || defined(__APPLE__)
    struct utsname system{};
    if (uname(&system)==0) {
        add(fields,"os.name",system.sysname,MetadataSource::RuntimeObserved,"uname");
        add(fields,"os.kernel_release",system.release,MetadataSource::RuntimeObserved,"uname");
        add(fields,"os.kernel_version",system.version,MetadataSource::RuntimeObserved,"uname");
    } else {
        for (const char* key : {"os.name","os.kernel_release","os.kernel_version"}) unavailable(fields,key,"uname failed");
    }
#else
    for (const char* key : {"os.name","os.kernel_release","os.kernel_version"}) unavailable(fields,key,"Runtime OS probe unavailable on this platform");
#endif
    buildFields(fields);
#ifdef _OPENMP
    add(fields,"openmp.specification_date",integer(_OPENMP),MetadataSource::BuildCaptured,"Compiler _OPENMP macro; NOT the runtime library version");
#else
    unavailable(fields,"openmp.specification_date","Compiler _OPENMP macro unavailable");
#endif
    unavailable(fields,"openmp.runtime_version","Standard OpenMP API does not expose the loaded runtime library version");
    const int processors=omp_get_num_procs();
    if (processors>0) add(fields,"hardware.logical_processors_visible",integer(processors),MetadataSource::RuntimeObserved,
                          "omp_get_num_procs; available logical processors, not physical cores");
    else unavailable(fields,"hardware.logical_processors_visible","OpenMP returned no positive processor count");
    add(fields,"openmp.context_max_threads",integer(omp_get_max_threads()),MetadataSource::RuntimeObserved);
    add(fields,"openmp.context_dynamic",integer(omp_get_dynamic()),MetadataSource::RuntimeObserved);
    add(fields,"openmp.context_thread_limit",integer(omp_get_thread_limit()),MetadataSource::RuntimeObserved);
    for (const char* name : {"OMP_NUM_THREADS","OMP_DYNAMIC","OMP_THREAD_LIMIT","OMP_PROC_BIND","OMP_PLACES",
                            "OMP_SCHEDULE","OMP_WAIT_POLICY","OMP_MAX_ACTIVE_LEVELS","OMP_STACKSIZE","OMP_DISPLAY_ENV",
                            "GOMP_CPU_AFFINITY","KMP_AFFINITY"}) {
        if (const char* value=std::getenv(name)) add(fields,std::string("environment.")+name,value,MetadataSource::RuntimeObserved);
        else unavailable(fields,std::string("environment.")+name,"Environment variable is unset");
    }
    std::set<std::string> keys;
    for (const auto& [key,value] : options.userFields) {
        if (key.empty() || !keys.insert(key).second) throw std::invalid_argument("Empty or duplicate user metadata key");
        export_detail::validateUtf8(key); export_detail::validateUtf8(value);
        add(fields,"user."+key,value,MetadataSource::UserProvided);
    }
    return context;
}
ExperimentMetadata describeExperiment(const ExperimentContext& context, const ExperimentResult& result) {
    auto metadata=base(context,"comparison",result.input);
    std::string order;
    switch(result.config.order) {
        case MeasurementOrder::AlternatingSequentialFirst: order="alternating_sequential_first"; break;
        case MeasurementOrder::AlternatingOpenMPFirst: order="alternating_openmp_first"; break;
        default: throw std::invalid_argument("Unknown comparison order");
    }
    executionFields(metadata,result.config.generations,result.config.warmups,result.config.repetitions,
                    result.completedWarmupRuns,result.samples.size(),integer(result.config.requestedThreads),order);
    return metadata;
}
ExperimentMetadata describeExperiment(const ExperimentContext& context, const ScalingResult& result) {
    auto metadata=base(context,"scaling",result.input);
    std::string threads,order;
    for (auto value : result.config.requestedThreads) { if(!threads.empty()) threads+=';'; threads+=integer(value); }
    switch(result.config.order) {
        case ScalingOrder::RotateLeft: order="rotate_left"; break;
        case ScalingOrder::RotateRight: order="rotate_right"; break;
        default: throw std::invalid_argument("Unknown scaling order");
    }
    executionFields(metadata,result.config.generations,result.config.warmups,result.config.repetitions,
                    result.completedWarmupRuns,result.samples.size(),threads,order);
    add(metadata.fields,"memory.estimated_peak_payload_bytes",integer(result.estimatedPeakBytes),MetadataSource::Result);
    add(metadata.fields,"memory.max_payload_bytes",integer(result.config.maxMemoryBytes),MetadataSource::Result);
    return metadata;
}
} // namespace conway::app

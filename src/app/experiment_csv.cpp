#include "app/experiment_csv.hpp"
#include "experiment_export_detail.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace conway::app {
namespace {
using export_detail::integer;
using export_detail::number;
using Row=std::vector<std::string>;
std::string boolean(bool value) { return value ? "true" : "false"; }
std::string optionalInt(std::optional<int> value) { return value ? integer(*value) : ""; }
std::string optionalDouble(std::optional<double> value) { return value ? number(*value) : ""; }

void row(std::ostream& output,const Row& values) {
    bool first=true;
    for (const auto& value : values) {
        export_detail::validateUtf8(value);
        if (!first) output.put(',');
        first=false;
        if (value.find_first_of(",\"\r\n") == std::string::npos) output << value;
        else {
            output.put('"');
            for(char c : value) { if(c=='"') output.put('"'); output.put(c); }
            output.put('"');
        }
    }
    output << "\r\n";
    if (!output) throw std::ios_base::failure("Failed to write experimental CSV");
}
void validateMetadata(const ExperimentMetadata& metadata) {
    if(metadata.studyId.empty()) throw std::invalid_argument("Empty study identifier");
    export_detail::validateUtf8(metadata.studyId);
    std::set<std::string> keys;
    for(const auto& field : metadata.fields) {
        if(field.key.empty() || !keys.insert(field.key).second)
            throw std::invalid_argument("Empty or duplicate metadata key");
        (void)metadataSourceName(field.source);
        if((field.source==MetadataSource::Unavailable) != !field.value.has_value()
           || (!field.value && field.detail.empty()))
            throw std::invalid_argument("Metadata availability/source is inconsistent");
        export_detail::validateUtf8(field.key); export_detail::validateUtf8(field.detail);
        if(field.value) export_detail::validateUtf8(*field.value);
    }
}
void validateStatistics(const std::optional<BenchmarkStatistics>& stats) {
    if(!stats) return;
    for(double value : {stats->minimumMs,stats->maximumMs,stats->meanMs,stats->medianMs,stats->populationStddevMs})
        if(!std::isfinite(value) || value<0) throw std::invalid_argument("Malformed stored statistics");
}
void validateSummary(const ExperimentSummary& summary,bool equivalent,bool valid,
                     std::optional<int> minimum,std::optional<int> maximum) {
    validateStatistics(summary.sequentialStatistics); validateStatistics(summary.openMPStatistics);
    if(summary.equivalent != equivalent || (summary.validForComparison && !valid))
        throw std::invalid_argument("Stored summary contradicts observations");
    const bool stable=minimum && maximum && *minimum>0 && minimum==maximum;
    if(summary.minEffectiveThreads!=minimum || summary.maxEffectiveThreads!=maximum
       || summary.effectiveThreadCountStable!=stable)
        throw std::invalid_argument("Stored team summary contradicts observations");
    if(summary.speedup && (!summary.equivalent || !summary.validForComparison
        || !summary.sequentialStatistics || !summary.openMPStatistics
        || summary.sequentialStatistics->medianMs<=0 || summary.openMPStatistics->medianMs<=0
        || !std::isfinite(*summary.speedup) || *summary.speedup<=0))
        throw std::invalid_argument("Stored speedup contradicts comparison validity");
    if(summary.efficiency && (!summary.speedup || !summary.effectiveThreadCountStable
        || !summary.minEffectiveThreads || *summary.minEffectiveThreads<=0
        || !summary.maxEffectiveThreads || summary.minEffectiveThreads!=summary.maxEffectiveThreads
        || !std::isfinite(*summary.efficiency) || *summary.efficiency<=0))
        throw std::invalid_argument("Stored efficiency contradicts team validity");
}
std::string invalidReason(const ExperimentSample& sample) {
    if(!std::isfinite(sample.measurement.elapsedMs)) return "non_finite_time";
    if(sample.measurement.elapsedMs<0) return "negative_time";
    if(!sample.measurement.equivalent) return "non_equivalent";
    if(!sample.validForComparison()) return "invalid_execution_metadata";
    return "";
}
std::string speedupStatus(const ExperimentSummary& summary) {
    if(!summary.equivalent) return "non_equivalent";
    if(!summary.validForComparison) return "invalid_comparison";
    if(!summary.sequentialStatistics || !summary.openMPStatistics) return "statistics_unavailable";
    if(summary.sequentialStatistics->medianMs<=0 || summary.openMPStatistics->medianMs<=0)
        return "insufficient_clock_resolution";
    return summary.speedup ? "available" : "unrepresentable_or_unavailable";
}
std::string efficiencyStatus(const ExperimentSummary& summary) {
    if(!summary.speedup) return "speedup_unavailable:"+speedupStatus(summary);
    if(!summary.minEffectiveThreads || !summary.maxEffectiveThreads || *summary.minEffectiveThreads<=0)
        return "invalid_effective_threads";
    if(!summary.effectiveThreadCountStable) return "variable_effective_threads";
    return summary.efficiency ? "available" : "unrepresentable_or_unavailable";
}
void headers(std::ostream& samples,std::ostream& summary,std::ostream& metadata) {
    row(samples,{"schema_version","study_id","experiment_type","configuration_id","engine","requested_threads",
        "effective_threads_min","effective_threads_max","repetition","round","position","execution_order",
        "sequential_reference_sample","generations","elapsed_ms","elapsed_ms_status","equivalent","valid","invalid_reason"});
    row(summary,{"schema_version","study_id","experiment_type","configuration_id","engine","requested_threads",
        "effective_threads_min","effective_threads_max","sample_count","min_ms","max_ms","mean_ms","median_ms",
        "stddev_population_ms","statistics_status","speedup","speedup_status","efficiency","efficiency_status",
        "equivalent","valid","invalid_reason"});
    row(metadata,{"study_id","key","value","source","detail"});
}
void sampleRow(std::ostream& output,const ExperimentMetadata& metadata,const ExperimentSample& sample,
               std::size_t reference,bool valid,std::string reason) {
    const auto& observation=sample.measurement;
    row(output,{"1",metadata.studyId,metadata.experimentType,export_detail::configurationId(observation.requestedThreads),
        observation.engine==Engine::Sequential ? "sequential" : "openmp",optionalInt(observation.requestedThreads),
        integer(observation.minEffectiveThreads),integer(observation.maxEffectiveThreads),integer(observation.repetitionIndex),
        integer(sample.roundIndex),integer(sample.positionInRound),integer(sample.executionIndex),integer(reference),
        integer(observation.generations),number(observation.elapsedMs),
        !std::isfinite(observation.elapsedMs) ? "non_finite" : observation.elapsedMs<0 ? "negative" : "valid",
        boolean(observation.equivalent),boolean(valid),std::move(reason)});
}
void summaryRow(std::ostream& output,const ExperimentMetadata& metadata,std::optional<int> threads,
                std::optional<int> minimum,std::optional<int> maximum,std::size_t count,
                const std::optional<BenchmarkStatistics>& stats,bool equivalent,bool valid,
                const ExperimentSummary* comparison) {
    row(output,{"1",metadata.studyId,metadata.experimentType,export_detail::configurationId(threads),
        threads ? "openmp" : "sequential",optionalInt(threads),optionalInt(minimum),optionalInt(maximum),integer(count),
        stats ? number(stats->minimumMs) : "", stats ? number(stats->maximumMs) : "",
        stats ? number(stats->meanMs) : "", stats ? number(stats->medianMs) : "",
        stats ? number(stats->populationStddevMs) : "",stats ? "available" : "unavailable_in_result",
        comparison ? optionalDouble(comparison->speedup) : "",comparison ? speedupStatus(*comparison) : "not_applicable",
        comparison ? optionalDouble(comparison->efficiency) : "",comparison ? efficiencyStatus(*comparison) : "not_applicable",
        boolean(equivalent),boolean(valid),!equivalent ? "non_equivalent" : !valid ? "invalid_observation_or_comparison" : ""});
}
void metadataRows(std::ostream& output,const ExperimentMetadata& metadata) {
    for(const auto& field : metadata.fields)
        row(output,{metadata.studyId,field.key,field.value.value_or(""),std::string(metadataSourceName(field.source)),field.detail});
}
void finish(std::ostream& a,std::ostream& b,std::ostream& c) {
    a.flush(); b.flush(); c.flush();
    if(!a || !b || !c) throw std::ios_base::failure("Failed to flush experimental CSV");
}
struct SeriesState {
    bool equivalent{true},valid{true};
    std::size_t count{0};
    std::optional<int> minimum,maximum;
    void observe(const ExperimentSample& sample) {
        equivalent=equivalent && sample.measurement.equivalent;
        valid=valid && sample.validForComparison();
        ++count;
        minimum=minimum ? std::min(*minimum,sample.measurement.minEffectiveThreads) : sample.measurement.minEffectiveThreads;
        maximum=maximum ? std::max(*maximum,sample.measurement.maxEffectiveThreads) : sample.measurement.maxEffectiveThreads;
    }
};
std::vector<std::size_t> validateResult(const ExperimentResult& result) {
    const auto& config=result.config;
    if(config.generations<=0 || config.warmups<0 || config.repetitions<=0 || config.requestedThreads<=0
       || (config.order!=MeasurementOrder::AlternatingSequentialFirst && config.order!=MeasurementOrder::AlternatingOpenMPFirst))
        throw std::invalid_argument("Malformed comparison configuration");
    const auto repetitions=static_cast<std::size_t>(config.repetitions);
    if(repetitions>std::numeric_limits<std::size_t>::max()/2 || result.samples.size()!=repetitions*2
       || result.completedWarmupRuns!=static_cast<std::size_t>(config.warmups)*2)
        throw std::invalid_argument("Malformed comparison sample/warmup count");
    std::vector<std::size_t> references(repetitions,std::numeric_limits<std::size_t>::max());
    SeriesState sequential,parallel;
    for(std::size_t index=0; index<result.samples.size(); ++index) {
        const auto& sample=result.samples[index];
        const auto& measured=sample.measurement;
        if(sample.roundIndex<0 || static_cast<std::size_t>(sample.roundIndex)>=repetitions
           || measured.repetitionIndex!=sample.roundIndex || measured.generations!=config.generations
           || sample.positionInRound!=static_cast<int>(index%2) || static_cast<std::size_t>(sample.roundIndex)!=index/2
           || sample.executionIndex!=1+result.completedWarmupRuns+index)
            throw std::invalid_argument("Malformed comparison sample audit");
        if(measured.engine==Engine::Sequential && !measured.requestedThreads) {
            auto& reference=references[static_cast<std::size_t>(sample.roundIndex)];
            if(reference!=std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("Duplicate Sequential sample");
            reference=index; sequential.observe(sample);
        } else if(measured.engine==Engine::OpenMP && measured.requestedThreads==config.requestedThreads) parallel.observe(sample);
        else throw std::invalid_argument("Malformed engine/request in comparison");
    }
    if(sequential.count!=repetitions || parallel.count!=repetitions
       || std::find(references.begin(),references.end(),std::numeric_limits<std::size_t>::max())!=references.end())
        throw std::invalid_argument("Incomplete comparison series");
    validateSummary(result.summary,sequential.equivalent && parallel.equivalent,sequential.valid && parallel.valid,
                    parallel.minimum,parallel.maximum);
    return references;
}
void validateResult(const ScalingResult& result) {
    const auto& config=result.config;
    if(config.generations<=0 || config.warmups<0 || config.repetitions<=0 || config.requestedThreads.empty()
       || (config.order!=ScalingOrder::RotateLeft && config.order!=ScalingOrder::RotateRight)
       || result.summary.points.size()!=config.requestedThreads.size())
        throw std::invalid_argument("Malformed scaling configuration/summary");
    std::set<std::int64_t> requests;
    for(auto value : config.requestedThreads)
        if(value<=0 || value>std::numeric_limits<int>::max() || !requests.insert(value).second)
            throw std::invalid_argument("Malformed scaling thread list");
    const auto count=config.requestedThreads.size()+1;
    const auto repetitions=static_cast<std::size_t>(config.repetitions);
    if(repetitions>std::numeric_limits<std::size_t>::max()/count || result.samples.size()!=count*repetitions
       || static_cast<std::size_t>(config.warmups)>std::numeric_limits<std::size_t>::max()/count
       || result.completedWarmupRuns!=count*static_cast<std::size_t>(config.warmups))
        throw std::invalid_argument("Malformed scaling sample/warmup count");
    std::vector<SeriesState> states(count);
    std::vector<bool> seen(count,false);
    for(std::size_t index=0; index<result.samples.size(); ++index) {
        const auto& sample=result.samples[index];
        const auto& execution=sample.execution;
        const auto& measured=execution.measurement;
        if(index%count==0) std::fill(seen.begin(),seen.end(),false);
        if(sample.configurationIndex>=count || seen[sample.configurationIndex]
           || execution.roundIndex<0 || static_cast<std::size_t>(execution.roundIndex)!=index/count
           || execution.positionInRound<0 || static_cast<std::size_t>(execution.positionInRound)!=index%count
           || measured.repetitionIndex!=execution.roundIndex || measured.generations!=config.generations
           || execution.executionIndex!=1+result.completedWarmupRuns+index
           || sample.sequentialReferenceIndex>=result.samples.size())
            throw std::invalid_argument("Malformed scaling sample audit");
        seen[sample.configurationIndex]=true;
        const auto& reference=result.samples[sample.sequentialReferenceIndex];
        if(reference.configurationIndex!=0 || reference.execution.roundIndex!=execution.roundIndex)
            throw std::invalid_argument("Malformed shared reference index");
        const auto request=sample.configurationIndex
            ? std::optional<int>{static_cast<int>(config.requestedThreads[sample.configurationIndex-1])} : std::nullopt;
        if(measured.engine!=(sample.configurationIndex ? Engine::OpenMP : Engine::Sequential) || measured.requestedThreads!=request)
            throw std::invalid_argument("Malformed scaling engine/request");
        states[sample.configurationIndex].observe(execution);
    }
    bool equivalent=true,valid=true;
    for(std::size_t index=1; index<count; ++index) {
        const auto& point=result.summary.points[index-1];
        if(point.requestedThreads!=config.requestedThreads[index-1]) throw std::invalid_argument("Scaling summary request mismatch");
        validateSummary(point.summary,states[0].equivalent && states[index].equivalent,states[0].valid && states[index].valid,
                        states[index].minimum,states[index].maximum);
        equivalent=equivalent && point.summary.equivalent;
        valid=valid && point.summary.validForComparison;
    }
    validateStatistics(result.summary.sequentialStatistics);
    if(result.summary.equivalent!=equivalent || (result.summary.validForComparison && !valid))
        throw std::invalid_argument("Scaling aggregate summary contradicts observations");
}
void serialize(const ExperimentResult& result,const ExperimentMetadata& metadata,
               std::ostream& samples,std::ostream& summary,std::ostream& meta) {
    const auto references=validateResult(result);
    headers(samples,summary,meta);
    SeriesState sequential,parallel;
    for(const auto& sample : result.samples) {
        sampleRow(samples,metadata,sample,references[static_cast<std::size_t>(sample.roundIndex)],
                  sample.validForComparison(),invalidReason(sample));
        (sample.measurement.engine==Engine::Sequential ? sequential : parallel).observe(sample);
    }
    summaryRow(summary,metadata,std::nullopt,sequential.minimum,sequential.maximum,sequential.count,
               result.summary.sequentialStatistics,sequential.equivalent,sequential.valid,nullptr);
    summaryRow(summary,metadata,result.config.requestedThreads,result.summary.minEffectiveThreads,result.summary.maxEffectiveThreads,
               parallel.count,result.summary.openMPStatistics,result.summary.equivalent,result.summary.validForComparison,&result.summary);
    metadataRows(meta,metadata); finish(samples,summary,meta);
}
void serialize(const ScalingResult& result,const ExperimentMetadata& metadata,
               std::ostream& samples,std::ostream& summary,std::ostream& meta) {
    validateResult(result);
    headers(samples,summary,meta);
    SeriesState sequential;
    for(const auto& sample : result.samples) {
        const auto& reference=result.samples[sample.sequentialReferenceIndex];
        auto reason=invalidReason(sample.execution);
        if(reason.empty() && !reference.execution.validForComparison()) reason="invalid_sequential_reference";
        sampleRow(samples,metadata,sample.execution,sample.sequentialReferenceIndex,sample.validForComparison(reference),reason);
        if(!sample.configurationIndex) sequential.observe(sample.execution);
    }
    summaryRow(summary,metadata,std::nullopt,sequential.minimum,sequential.maximum,sequential.count,
               result.summary.sequentialStatistics,sequential.equivalent,sequential.valid,nullptr);
    for(const auto& point : result.summary.points)
        summaryRow(summary,metadata,point.requestedThreads,point.summary.minEffectiveThreads,point.summary.maxEffectiveThreads,
                   static_cast<std::size_t>(result.config.repetitions),point.summary.openMPStatistics,
                   point.summary.equivalent,point.summary.validForComparison,&point.summary);
    metadataRows(meta,metadata); finish(samples,summary,meta);
}
template<class Result>
CsvExportPaths exportFiles(const Result& result,const ExperimentContext& context,const std::filesystem::path& directory) {
    (void)validateResult(result);
    const auto metadata=describeExperiment(context,result);
    validateMetadata(metadata);
    if(!std::filesystem::create_directory(directory))
        throw std::filesystem::filesystem_error("Export destination already exists",directory,std::make_error_code(std::errc::file_exists));
    CsvExportPaths paths{directory/"samples.csv",directory/"summary.csv",directory/"metadata.csv"};
    try {
        std::ofstream samples(paths.samples,std::ios::binary),summary(paths.summary,std::ios::binary),meta(paths.metadata,std::ios::binary);
        if(!samples || !summary || !meta) throw std::ios_base::failure("Cannot open experimental CSV output");
        serialize(result,metadata,samples,summary,meta);
        samples.close(); summary.close(); meta.close();
        if(!samples || !summary || !meta) throw std::ios_base::failure("Cannot close experimental CSV output");
    } catch(...) {
        std::error_code ignored;
        for(const auto& path : {paths.samples,paths.summary,paths.metadata}) std::filesystem::remove(path,ignored);
        std::filesystem::remove(directory,ignored);
        throw;
    }
    return paths;
}
} // namespace

void writeExperimentCsv(const ExperimentResult& result,const ExperimentContext& context,
                        std::ostream& samples,std::ostream& summary,std::ostream& metadata) {
    const auto description=describeExperiment(context,result); validateMetadata(description);
    serialize(result,description,samples,summary,metadata);
}
void writeExperimentCsv(const ScalingResult& result,const ExperimentContext& context,
                        std::ostream& samples,std::ostream& summary,std::ostream& metadata) {
    const auto description=describeExperiment(context,result); validateMetadata(description);
    serialize(result,description,samples,summary,metadata);
}
CsvExportPaths exportExperimentCsv(const ExperimentResult& result,const ExperimentContext& context,const std::filesystem::path& directory) {
    return exportFiles(result,context,directory);
}
CsvExportPaths exportExperimentCsv(const ScalingResult& result,const ExperimentContext& context,const std::filesystem::path& directory) {
    return exportFiles(result,context,directory);
}
} // namespace conway::app

#include "app/experimental_study.hpp"
#include "benchmark_detail.hpp"
#include "experiment_input_detail.hpp"
#include "mpi/counts.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>

namespace conway::app {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
bool timeValid(double value) {return std::isfinite(value) && value>=0;}
std::size_t at(bool right,std::size_t round,std::size_t position,std::size_t count) {
    return right?(position+count-round%count)%count:(position+round)%count;
}
void validateConfiguration(const ExecutionConfiguration& c) {
    if(c.engine==Engine::Sequential && !c.requestedThreads && !c.requestedProcesses) return;
    if(c.engine==Engine::OpenMP && c.requestedThreads && *c.requestedThreads>0 && !c.requestedProcesses) return;
    if(c.engine==Engine::MPI && !c.requestedThreads && c.requestedProcesses &&
       *c.requestedProcesses>0 && *c.requestedProcesses<=1024) return;
    throw std::invalid_argument("Invalid engine/resource configuration");
}
bool equivalent(const mpi::RemoteSnapshot& snapshot,const Game& reference) {
    if(snapshot.input.width!=reference.width() || snapshot.input.height!=reference.height() ||
       snapshot.generation!=reference.generation() || snapshot.aliveCount!=reference.aliveCount()) return false;
    for(std::size_t r=0;r<reference.height();++r)
        for(std::size_t c=0;c<reference.width();++c)
            if(snapshot.input.cells[r*reference.width()+c]!=reference.isAlive(r,c)) return false;
    return true;
}
bool validSample(const StudySample& s,const ExecutionConfiguration& c) {
    if(!s.equivalent || !timeValid(s.elapsedMs)) return false;
    if(c.engine==Engine::MPI) return !s.minEffectiveThreads && !s.maxEffectiveThreads &&
        s.observedProcesses==c.requestedProcesses && s.sessionIndex && s.loadMs && s.snapshotMs && s.requestMs &&
        timeValid(*s.loadMs) && timeValid(*s.snapshotMs) && timeValid(*s.requestMs);
    if(s.observedProcesses || s.sessionIndex || s.loadMs || s.snapshotMs || s.requestMs ||
       !s.minEffectiveThreads || !s.maxEffectiveThreads || *s.minEffectiveThreads<1 ||
       *s.maxEffectiveThreads<*s.minEffectiveThreads) return false;
    return c.engine==Engine::OpenMP || (*s.minEffectiveThreads==1 && *s.maxEffectiveThreads==1);
}
bool validSession(const ExperimentalStudy& study,const StudySample& sample) {
    if(!sample.sessionIndex || *sample.sessionIndex>=study.sessions.size()) return false;
    const auto& session=study.sessions[*sample.sessionIndex];
    const auto& config=study.configurations[sample.configurationIndex];
    return config.engine==Engine::MPI && config.requestedProcesses &&
        session.configurationIndex==sample.configurationIndex && session.round==sample.round &&
        session.cleanupConfirmed && session.diagnostic.empty() &&
        session.workerPids.size()==static_cast<std::size_t>(*config.requestedProcesses) &&
        session.completedWarmups==static_cast<std::size_t>(study.warmups) &&
        sample.completedWarmups==session.completedWarmups;
}

}
mpi::ClientOptions experimentalMPIOptions() {
    mpi::ClientOptions options;
    options.launcher=CONWAY_EXPERIMENT_MPI_LAUNCHER;
    std::error_code error;
    const auto executable=std::filesystem::read_symlink("/proc/self/exe",error);
    options.worker=(error?std::filesystem::path("conway_mpi_worker"):executable.parent_path()/"conway_mpi_worker").string();
    return options;
}
std::string configurationName(const ExecutionConfiguration& c) {
    validateConfiguration(c);
    if(c.engine==Engine::Sequential) return "sequential";
    return c.engine==Engine::OpenMP?"openmp:"+std::to_string(*c.requestedThreads):"mpi:"+std::to_string(*c.requestedProcesses);
}
ExperimentalStudy runExperimentalStudy(const InputData& input,std::vector<ExecutionConfiguration> configurations,
        int generations,int warmups,int repetitions,bool right,mpi::ClientOptions options,std::size_t budget) {
    (void)detail::validateInput(input);
    if(configurations.size()<2 || generations<1 || warmups<0 || repetitions<1)
        throw std::invalid_argument("Invalid experimental study dimensions/counts");
    std::set<std::string> names;
    bool hasMPI=false;
    std::size_t maximumProcesses=0;
    for(const auto& c:configurations) {
        if(!names.insert(configurationName(c)).second) throw std::invalid_argument("Duplicate experimental configuration");
        hasMPI=hasMPI || c.engine==Engine::MPI;
        if(c.engine==Engine::MPI) maximumProcesses=std::max(maximumProcesses,static_cast<std::size_t>(*c.requestedProcesses));
    }
    if(hasMPI && (generations>100000 || mpi::checked_cell_count(input.width,input.height)>mpi::wire::maxPayload-32))
        throw std::invalid_argument("MPI batch/input exceeds worker limits");
    if(options.startupTimeout.count()<=0 || options.operationTimeout.count()<=0 || options.closeTimeout.count()<=0)
        throw std::invalid_argument("Invalid experimental MPI timeout");
    auto defaults=experimentalMPIOptions();
    if(options.launcher.empty()) options.launcher=defaults.launcher;
    if(options.worker.empty()) options.worker=defaults.worker;
    const auto count=detail::checkedMultiply(configurations.size(),static_cast<std::size_t>(repetitions));
    const auto warming=detail::checkedMultiply(count,static_cast<std::size_t>(warmups));
    (void)detail::checkedAdd(1,detail::checkedAdd(count,warming));
    const auto padded=detail::checkedMultiply(detail::checkedAdd(input.width,2),detail::checkedAdd(input.height,2));
    auto bytes=detail::checkedAdd(input.cells.capacity(),detail::checkedMultiply(6,padded));
    bytes=detail::checkedAdd(bytes,detail::checkedMultiply(count,sizeof(StudySample)+sizeof(StudySession)+8192+1024*sizeof(int)));
    // Worker double buffers include halos on empty ranks too. Runtime/allocator
    // overhead remains outside this payload policy; sessions never overlap.
    if(hasMPI) {
        const auto rows=detail::checkedAdd(input.height,detail::checkedMultiply(2,maximumProcesses));
        const auto workerBuffers=detail::checkedMultiply(2,detail::checkedMultiply(rows,detail::checkedAdd(input.width,2)));
        bytes=detail::checkedAdd(bytes,workerBuffers);
        bytes=detail::checkedAdd(bytes,detail::checkedMultiply(2,input.cells.size()));
    }
    bytes=detail::checkedAdd(bytes,128u*1024u); // Bounded failure diagnostics and duplicate context.
    if(bytes>budget) throw std::length_error("Experimental study exceeds payload budget");
    ExperimentalStudy study;
    study.configurations=std::move(configurations); study.generations=generations; study.warmups=warmups;
    study.repetitions=repetitions; study.rotateRight=right; study.mpiOptions=options;
    study.estimatedPeakBytes=bytes; study.maxMemoryBytes=budget;
    study.samples.reserve(count); study.sessions.reserve(count);
    detail::OpenMPSettingsGuard settings; omp_set_dynamic(0);
    auto reference=detail::runOnce(input,Engine::Sequential,1,generations);
    std::size_t execution=1;
    try {
    // Local warmups are per configuration, MPI warmups per freshly created session.
    for(std::size_t round=0;round<static_cast<std::size_t>(warmups);++round)
        for(std::size_t pos=0;pos<study.configurations.size();++pos) {
            const auto& c=study.configurations[at(right,round,pos,study.configurations.size())];
            if(c.engine==Engine::MPI) continue;
            auto run=detail::runOnce(input,c.engine,c.requestedThreads.value_or(1),generations);
            ++study.completedWarmupRuns; ++study.completedLocalWarmups; ++execution;
            if(!detail::sameState(run.finalGame,reference.finalGame)) {
                study.diagnostic="Local warmup is not equivalent to sequential oracle";
                study.summaries=summarizeStudy(study); return study;
            }
        }
    for(int round=0;round<repetitions;++round) {
        for(std::size_t pos=0;pos<study.configurations.size();++pos) {
            const auto index=at(right,round,pos,study.configurations.size());
            const auto& c=study.configurations[index];
            StudySample sample; sample.configurationIndex=index; sample.round=round; sample.position=static_cast<int>(pos);
            if(c.engine!=Engine::MPI) {
                auto run=detail::runOnce(input,c.engine,c.requestedThreads.value_or(1),generations);
                sample.executionIndex=execution++; sample.elapsedMs=run.elapsedMs;
                sample.minEffectiveThreads=run.minEffectiveThreads; sample.maxEffectiveThreads=run.maxEffectiveThreads;
                sample.equivalent=detail::sameState(run.finalGame,reference.finalGame);
                sample.valid=validSample(sample,c); study.samples.push_back(sample);
            } else {
                const auto sessionIndex=study.sessions.size();
                study.sessions.emplace_back(); auto& session=study.sessions.back();
                session.configurationIndex=index; session.round=round;
                std::unique_ptr<mpi::Client> client;
                const auto startupStart=Clock::now();
                try {
                    auto launch=options; launch.processes=*c.requestedProcesses;
                    auto start=Clock::now(); client=std::make_unique<mpi::Client>(launch); session.startupMs=elapsed(start);
                    session.workerPids=client->workerPids(); session.declaredSlots=client->declaredSlots();
                    for(std::size_t attempt=0;attempt<=static_cast<std::size_t>(warmups);++attempt) {
                        start=Clock::now(); client->load(input,0); const double load=elapsed(start);
                        start=Clock::now(); auto batch=client->runBatch(generations,0); const double request=elapsed(start);
                        start=Clock::now(); auto snapshot=client->snapshot(); const double snapshotTime=elapsed(start);
                        session.runtime=batch.runtime;
                        const bool same=equivalent(snapshot,reference.finalGame);
                        if(attempt<static_cast<std::size_t>(warmups)) {
                            ++session.completedWarmups; ++study.completedWarmupRuns; ++execution;
                            if(!same) throw std::runtime_error("MPI warmup is not equivalent to sequential oracle");
                        } else {
                            sample.executionIndex=execution++; sample.completedWarmups=session.completedWarmups;
                            sample.elapsedMs=batch.maximumMs; sample.observedProcesses=batch.processes;
                            sample.sessionIndex=sessionIndex; sample.loadMs=load; sample.requestMs=request; sample.snapshotMs=snapshotTime;
                            sample.equivalent=same; sample.valid=validSample(sample,c); study.samples.push_back(sample);
                        }
                    }
                    start=Clock::now(); client->close(); session.closeMs=elapsed(start);
                    session.cleanupConfirmed=client->cleanupConfirmed();
                    if(!session.cleanupConfirmed) throw std::runtime_error("MPI cleanup could not be confirmed");
                } catch(const std::exception& error) {
                    session.diagnostic=error.what();
                    if(!client) session.startupMs=elapsed(startupStart);
                    if(client) {
                        try {client->close();} catch(const std::exception& closing) {session.diagnostic+="; close: "+std::string(closing.what());}
                        session.cleanupConfirmed=client->cleanupConfirmed();
                    } else if(const auto* failure=dynamic_cast<const mpi::ClientError*>(&error))
                        session.cleanupConfirmed=failure->cleanupConfirmed;
                    study.cleanupConfirmed=session.cleanupConfirmed;
                    study.diagnostic="MPI configuration "+configurationName(c)+", round "+std::to_string(round)+": "+session.diagnostic;
                    study.summaries=summarizeStudy(study); return study; // No retries or later participants.
                }
            }
        }
    }
    } catch(const std::exception& error) {
        study.diagnostic="Local experimental execution: "+std::string(error.what());
        study.summaries=summarizeStudy(study); return study;
    }
    study.complete=true; study.summaries=summarizeStudy(study); return study;
}
std::vector<StudySummary> summarizeStudy(const ExperimentalStudy& study) {
    std::vector<StudySummary> summaries;
    std::optional<std::size_t> sequential;
    for(std::size_t index=0;index<study.configurations.size();++index) {
        const auto& c=study.configurations[index]; validateConfiguration(c);
        StudySummary summary; summary.configurationIndex=index; summary.equivalent=true;
        summary.valid=study.complete && study.cleanupConfirmed;
        std::vector<double> times;
        bool validTimes=true;
        if(c.engine==Engine::Sequential) sequential=index;
        for(const auto& s:study.samples) if(s.configurationIndex==index) {
            ++summary.sampleCount; summary.equivalent=summary.equivalent && s.equivalent;
            summary.valid=summary.valid && s.valid && validSample(s,c);
            if(c.engine==Engine::MPI) summary.valid=summary.valid && validSession(study,s);
            validTimes=validTimes && timeValid(s.elapsedMs); times.push_back(s.elapsedMs);
            const auto minimum=c.engine==Engine::MPI?s.observedProcesses:s.minEffectiveThreads;
            const auto maximum=c.engine==Engine::MPI?s.observedProcesses:s.maxEffectiveThreads;
            if(minimum) summary.minimumResources=summary.minimumResources?std::min(*summary.minimumResources,*minimum):minimum;
            if(maximum) summary.maximumResources=summary.maximumResources?std::max(*summary.maximumResources,*maximum):maximum;
        }
        summary.equivalent=summary.equivalent && summary.sampleCount>0;
        summary.valid=summary.valid && summary.sampleCount==static_cast<std::size_t>(study.repetitions);
        if(validTimes) summary.statistics=calculateBenchmarkStatistics(times);
        summaries.push_back(summary);
    }
    if(sequential) {
        const auto& ref=summaries[*sequential];
        for(auto& summary:summaries) if(summary.configurationIndex!=*sequential) {
            summary.valid=summary.valid && ref.valid;
            if(summary.valid && ref.statistics && summary.statistics && ref.statistics->medianMs>0 && summary.statistics->medianMs>0) {
                const double speedup=ref.statistics->medianMs/summary.statistics->medianMs;
                if(std::isfinite(speedup) && speedup>0) summary.speedup=speedup;
                if(summary.speedup && summary.minimumResources && summary.minimumResources==summary.maximumResources) {
                    const double efficiency=speedup / *summary.minimumResources;
                    if(std::isfinite(efficiency) && efficiency>0) summary.efficiency=efficiency;
                }
            }
        }
    }
    return summaries;
}
void validateStudy(const ExperimentalStudy& study) {
    if(study.configurations.size()<2 || study.repetitions<1 || study.generations<1 || study.warmups<0)
        throw std::invalid_argument("Malformed study");
    if(!study.complete && study.diagnostic.empty()) throw std::invalid_argument("Incomplete study has no diagnostic");
    std::set<std::string> names;
    for(const auto& c:study.configurations) if(!names.insert(configurationName(c)).second)
        throw std::invalid_argument("Duplicate study configuration");
    std::size_t previous=0, execution=1+study.completedLocalWarmups;
    std::size_t completed=study.completedLocalWarmups, nextSession=0;
    for(const auto& session:study.sessions) completed+=session.completedWarmups;
    if(completed!=study.completedWarmupRuns) throw std::invalid_argument("Invalid study warmup count");
    const auto locals=std::count_if(study.configurations.begin(),study.configurations.end(),[](const auto& c){return c.engine!=Engine::MPI;});
    if(study.complete && study.completedLocalWarmups!=static_cast<std::size_t>(locals)*study.warmups)
        throw std::invalid_argument("Invalid local warmup count");
    for(std::size_t i=0;i<study.samples.size();++i) {
        const auto& sample=study.samples[i];
        const auto round=i/study.configurations.size(),position=i%study.configurations.size();
        if(sample.round!=static_cast<int>(round) || sample.position!=static_cast<int>(position) ||
           round>=static_cast<std::size_t>(study.repetitions) || sample.configurationIndex!=at(study.rotateRight,round,position,study.configurations.size()) ||
           sample.executionIndex<=previous || sample.valid!=validSample(sample,study.configurations[sample.configurationIndex]))
            throw std::invalid_argument("Study acquisition audit is inconsistent");
        const bool mpi=study.configurations[sample.configurationIndex].engine==Engine::MPI;
        if(mpi!=sample.sessionIndex.has_value())
            throw std::invalid_argument("Sample session does not match its engine");
        if(mpi) execution+=study.warmups;
        if(sample.executionIndex!=execution++) throw std::invalid_argument("Invalid sample execution order");
        previous=sample.executionIndex;
        if(sample.sessionIndex) {
            if(*sample.sessionIndex!=nextSession++ || *sample.sessionIndex>=study.sessions.size()) throw std::invalid_argument("Missing MPI session");
            const auto& session=study.sessions[*sample.sessionIndex];
            if(session.configurationIndex!=sample.configurationIndex || session.round!=sample.round ||
               session.workerPids.size()!=static_cast<std::size_t>(*study.configurations[sample.configurationIndex].requestedProcesses) ||
               sample.completedWarmups!=session.completedWarmups || session.completedWarmups!=static_cast<std::size_t>(study.warmups))
                throw std::invalid_argument("MPI session audit is inconsistent");
        }
    }
    if(study.complete && nextSession!=study.sessions.size()) throw std::invalid_argument("Unreferenced MPI session");
    if(study.complete && (study.samples.size()!=study.configurations.size()*study.repetitions || !study.diagnostic.empty() || !study.cleanupConfirmed))
        throw std::invalid_argument("Malformed completed study");
    for(const auto& session:study.sessions)
        if(!timeValid(session.startupMs) || !timeValid(session.closeMs) ||
           session.configurationIndex>=study.configurations.size() || study.configurations[session.configurationIndex].engine!=Engine::MPI ||
           (study.complete && (!session.cleanupConfirmed || !session.diagnostic.empty() || session.runtime.empty())))
            throw std::invalid_argument("Malformed MPI session");
    const auto expected=summarizeStudy(study);
    if(expected.size()!=study.summaries.size()) throw std::invalid_argument("Missing study summary");
    auto sameStatistics=[](const std::optional<BenchmarkStatistics>& a,const std::optional<BenchmarkStatistics>& b) {
        if(a.has_value()!=b.has_value()) return false;
        return !a || (a->minimumMs==b->minimumMs && a->maximumMs==b->maximumMs &&
            a->meanMs==b->meanMs && a->medianMs==b->medianMs && a->populationStddevMs==b->populationStddevMs);
    };
    for(std::size_t i=0;i<expected.size();++i) {
        const auto& a=expected[i];const auto& b=study.summaries[i];
        if(a.configurationIndex!=b.configurationIndex || a.sampleCount!=b.sampleCount ||
           a.equivalent!=b.equivalent || a.valid!=b.valid || a.minimumResources!=b.minimumResources ||
           a.maximumResources!=b.maximumResources || a.speedup!=b.speedup || a.efficiency!=b.efficiency ||
           !sameStatistics(a.statistics,b.statistics)) throw std::invalid_argument("Stale study summary");
    }

}
} // namespace conway::app

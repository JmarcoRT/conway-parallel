#include "app/experiment_csv.hpp"
#include "app/experiment_execution.hpp"
#include <condition_variable>
#include <filesystem>
#include <iostream>

using namespace conway;
using namespace conway::app;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F f) {bool rejected=false;try {f();}catch(const std::exception&) {rejected=true;}check(rejected,"invalid audit was accepted");}
ExperimentResult fixture() {
    ExperimentResult result;
    result.config.generations=3;result.config.warmups=0;result.config.repetitions=1;
    result.config.participants={{Engine::Sequential,{},{}},{Engine::OpenMP,2,{}},{Engine::MPI,{},2}};
    result.input={5,3,3,0,{}};
    ExperimentalStudy study;
    study.configurations=result.config.participants;study.generations=3;study.warmups=0;study.repetitions=1;study.complete=true;
    StudySession session;session.configurationIndex=2;session.round=0;session.declaredSlots=2;
    session.workerPids={10,11};session.runtime="synthetic-runtime";session.cleanupConfirmed=true;
    study.sessions.push_back(session);
    for(std::size_t i=0;i<3;++i) {
        StudySample sample;sample.configurationIndex=i;sample.round=0;sample.position=i;sample.executionIndex=i+1;
        sample.elapsedMs=i==0?10:i==1?5:20;sample.equivalent=sample.valid=true;
        if(i<2) {sample.minEffectiveThreads=i==0?1:2;sample.maxEffectiveThreads=sample.minEffectiveThreads;}
        else {sample.observedProcesses=2;sample.sessionIndex=0;sample.loadMs=1;sample.snapshotMs=1;sample.requestMs=21;}
        study.samples.push_back(sample);
    }
    study.summaries=summarizeStudy(study);result.study=study;return result;
}
void refresh(ExperimentResult& result) {result.study->summaries=summarizeStudy(*result.study);}
int main(int argc,char** argv) {
    try {
        check(argc==2,"output parent required");
        auto good=fixture();validateStudy(*good.study);
        auto localSession=good;localSession.study->samples[0].sessionIndex=0;localSession.study->samples[0].valid=false;
        refresh(localSession);rejects([&]{validateStudy(*localSession.study);});
        auto missingSession=good;missingSession.study->samples[2].sessionIndex.reset();missingSession.study->samples[2].valid=false;
        refresh(missingSession);rejects([&]{validateStudy(*missingSession.study);});
        auto unclosed=good;unclosed.study->sessions[0].cleanupConfirmed=false;refresh(unclosed);
        check(!unclosed.study->summaries[2].speedup && !unclosed.study->summaries[2].efficiency,"unconfirmed session produced MPI metrics");
        rejects([&]{validateStudy(*unclosed.study);});
        auto partial=good;partial.study->complete=false;partial.study->diagnostic="confirmed close failure after final sample";
        refresh(partial);validateStudy(*partial.study);
        for(const auto& summary:partial.study->summaries) check(!summary.valid && !summary.speedup && !summary.efficiency,"partial study appears complete");
        auto noDiagnostic=partial;noDiagnostic.study->diagnostic.clear();refresh(noDiagnostic);
        rejects([&]{validateStudy(*noDiagnostic.study);});
        // Even a backend returning an incomplete result without a diagnostic cannot publish Finished.
        std::mutex mutex;std::condition_variable changed;
        auto context=captureExperimentContext({"review",std::string("synthetic"),{}});
        ExperimentExecution controller([&]{changed.notify_all();},[&](const auto& request,const auto&) {
            return StoredExperiment{request.description,context,noDiagnostic};
        });
        ExperimentFormConfig form;form.comparison=noDiagnostic.config;InputData input{1,1,{1}};
        check(controller.start(false,form,&input,"synthetic"),"controller start");
        {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return !controller.snapshot().active();}),"controller timeout");}
        check(controller.snapshot().phase==ExecutionPhase::Error && !controller.snapshot().error.empty() &&
              controller.snapshot().latest,"partial evidence lost or published Finished");
        controller.shutdown();
        auto badResources=good;badResources.study->samples[2].observedProcesses=3;badResources.study->samples[2].valid=false;
        refresh(badResources);validateStudy(*badResources.study);
        check(!badResources.study->summaries[2].speedup && !badResources.study->summaries[2].efficiency,"invalid processes produced metrics");
        auto badReference=good;badReference.study->samples[0].equivalent=false;badReference.study->samples[0].valid=false;
        refresh(badReference);validateStudy(*badReference.study);
        for(std::size_t i=1;i<3;++i) check(!badReference.study->summaries[i].speedup && !badReference.study->summaries[i].efficiency,"invalid sequential reference produced metrics");
        const auto parent=std::filesystem::path(argv[1]);
        exportExperimentCsv(good,context,parent/"good");exportExperimentCsv(partial,context,parent/"partial");
        exportExperimentCsv(badResources,context,parent/"bad-resources");exportExperimentCsv(badReference,context,parent/"bad-reference");
        std::cout<<"PASS engine/session audit, resource/reference validity, incomplete delivery and CSV v2\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

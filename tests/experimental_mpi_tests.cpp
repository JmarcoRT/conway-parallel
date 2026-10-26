#include "app/experiment_runner.hpp"
#include "app/scaling_study.hpp"
#include "app/experiment_csv.hpp"
#include "app/experiment_execution.hpp"
#include <condition_variable>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <thread>

using namespace conway;
using namespace conway::app;
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
template<class F> void rejects(F f) {bool caught=false;try {f();}catch(const std::exception&) {caught=true;}check(caught,"expected rejection");}
void verify(const ExperimentalStudy& study) {
    check(study.complete && study.cleanupConfirmed && study.diagnostic.empty(),study.diagnostic.c_str());
    validateStudy(study);
    for(const auto& s:study.samples) check(s.valid && s.equivalent && std::isfinite(s.elapsedMs) && s.elapsedMs>=0,"invalid sample");
    for(const auto& summary:study.summaries) check(summary.valid && summary.equivalent && summary.statistics.has_value(),"invalid summary");
    for(const auto& session:study.sessions) {
        check(session.cleanupConfirmed && session.completedWarmups==static_cast<std::size_t>(study.warmups),"session warmup/cleanup");
        for(auto pid:session.workerPids) check(kill(pid,0)<0,"residual worker");
    }
}
int main(int argc,char** argv) {
    try {
        check(argc==5,"launcher, worker, Python and guard required");
        mpi::ClientOptions options; options.launcher=argv[1];options.worker=argv[2]; options.processes=2;
        options.launcherArguments={"--oversubscribe"};
        InputData input{5,3,std::vector<std::uint8_t>(15)};input.cells[6]=input.cells[7]=input.cells[8]=1;
        Game reference(input); for(int i=0;i<3;++i) reference.step();
        {
            mpi::Client client(options);
            for(int i=0;i<3;++i) {
                client.load(input);auto metrics=client.runBatch(3);auto board=client.snapshot();
                check(metrics.processes==2 && metrics.generation==3 && !metrics.runtime.empty() &&
                    std::isfinite(metrics.maximumMs) && metrics.maximumMs>=0,"batch metrics");
                check(board.generation==3 && board.aliveCount==reference.aliveCount(),"reload did not reset batch");
                for(std::size_t r=0;r<input.height;++r) for(std::size_t c=0;c<input.width;++c)
                    check(board.input.cells[r*input.width+c]==reference.isAlive(r,c),"batch state mismatch");
            }
            rejects([&]{client.runBatch(0);});
            rejects([&]{client.runBatch(1,0);}); // Worker rejects stale generation before mutation.
            check(client.cleanupConfirmed(),"invalid request cleanup not confirmed");
        }
        // This wrapper rejects overlap at each subsequent launch, without timing assumptions.
        auto guarded=options;
        guarded.launcher=argv[3];guarded.launcherArguments={argv[4],argv[1],"--oversubscribe"};
        ExperimentConfig config; config.generations=3;config.warmups=1;config.repetitions=3;config.mpiOptions=guarded;
        config.participants={{Engine::Sequential,{},{}},{Engine::OpenMP,2,{}},{Engine::MPI,{},2}};
        auto comparison=runExperiment(input,config);verify(*comparison.study);
        check(comparison.study->sessions.size()==3 && comparison.completedWarmupRuns==5,"warmup counts");
        for(int r=0;r<3;++r) for(int pos=0;pos<3;++pos)
            check(comparison.study->samples[r*3+pos].configurationIndex==static_cast<std::size_t>((r+pos)%3),"comparison rotation");
        auto synthetic=*comparison.study;
        for(auto& s:synthetic.samples) s.elapsedMs=(s.configurationIndex==0?10.0:s.configurationIndex==1?5.0:20.0)+s.round;
        synthetic.summaries=summarizeStudy(synthetic);validateStudy(synthetic);
        check(synthetic.summaries[0].statistics->medianMs==11 &&
              synthetic.summaries[1].speedup==std::optional<double>{11.0/6.0} &&
              synthetic.summaries[2].efficiency==std::optional<double>{(11.0/21.0)/2.0},"statistics/resource formulas");
        auto variable=synthetic;
        for(auto& s:variable.samples) if(s.configurationIndex==1) {s.minEffectiveThreads=1;s.maxEffectiveThreads=2;}
        variable.summaries=summarizeStudy(variable);validateStudy(variable);
        check(variable.summaries[1].speedup && !variable.summaries[1].efficiency,"variable team efficiency");
        auto zero=synthetic;
        for(auto& s:zero.samples) if(s.configurationIndex==2) s.elapsedMs=0;
        zero.summaries=summarizeStudy(zero);validateStudy(zero);
        check(!zero.summaries[2].speedup && !zero.summaries[2].efficiency,"zero time is not acceleration");
        auto different=synthetic;
        for(auto& s:different.samples) if(s.configurationIndex==2) {s.equivalent=false;s.valid=false;break;}
        different.summaries=summarizeStudy(different);validateStudy(different);
        check(!different.summaries[2].valid && !different.summaries[2].speedup,"non-equivalence allowed performance metric");
        ScalingConfig scaling;scaling.parallelEngine=Engine::MPI;scaling.requestedProcesses={1,4};
        scaling.generations=3;scaling.warmups=1;scaling.repetitions=2;scaling.mpiOptions=guarded;
        scaling.order=ScalingOrder::RotateRight;
        auto result=runScalingStudy(input,scaling);verify(*result.study);
        check(result.study->sessions.size()==4 && result.completedWarmupRuns==5,"scaling warmup counts");
        check(result.study->samples.size()==6,"shared sequential series duplicated");
        auto context=captureExperimentContext({"mpi-small",std::string("test"),{}});
        std::ostringstream a,b,c;writeExperimentCsv(result,context,a,b,c);
        check(a.str().find("requested_processes")!=std::string::npos && a.str().find("mpi:4")!=std::string::npos &&
              c.str().find("csv.schema_version,2")!=std::string::npos && c.str().find("session.3.cleanup_confirmed,true")!=std::string::npos,"CSV v2 missing MPI audit");
        std::ostringstream ca,cb,cc;writeExperimentCsv(comparison,context,ca,cb,cc);
        check(ca.str().find("openmp:2")!=std::string::npos,"comparison CSV missing OpenMP");
        auto damaged=*result.study;damaged.samples[0].position=99;rejects([&]{validateStudy(damaged);});
        damaged=*result.study;damaged.summaries[0].valid=false;rejects([&]{validateStudy(damaged);});
        auto noReference=config;noReference.warmups=0;noReference.repetitions=1;
        noReference.participants={{Engine::OpenMP,1,{}},{Engine::MPI,{},1}};
        auto pair=runExperiment(input,noReference);verify(*pair.study);
        for(const auto& s:pair.study->summaries) check(!s.speedup && !s.efficiency,"invented sequential reference");
        auto inconsistent=result;inconsistent.config.requestedProcesses={2,4};
        rejects([&]{std::ostringstream x,y,z;writeExperimentCsv(inconsistent,context,x,y,z);});
        auto constrained=config;constrained.maxMemoryBytes=1;rejects([&]{runExperiment(input,constrained);});
        auto invalid=config;invalid.participants.push_back(invalid.participants.back());rejects([&]{runExperiment(input,invalid);});
        invalid=config;invalid.participants.back().requestedThreads=2;rejects([&]{runExperiment(input,invalid);});
        invalid=config;invalid.generations=100001;rejects([&]{runExperiment(input,invalid);});
        auto duplicate=scaling;duplicate.requestedProcesses={2,2};rejects([&]{runScalingStudy(input,duplicate);});
        auto broken=config;broken.mpiOptions.launcher="/no/such/mpi-launcher";
        auto failed=runExperiment(input,broken);
        check(!failed.study->complete && failed.study->cleanupConfirmed && !failed.study->diagnostic.empty() &&
            failed.study->samples.size()==2 && failed.study->sessions.size()==1,"failure retried/lost evidence");
        std::ostringstream fa,fb,fc;writeExperimentCsv(failed,context,fa,fb,fc);
        check(fc.str().find("execution.complete,false")!=std::string::npos,"partial failure cannot export");
        // Delivery keeps the partial failure and exposes Error rather than Finished.
        std::mutex mutex;std::condition_variable changed;
        ExperimentExecution controller([&]{changed.notify_all();},[&](const auto& request,const auto&) {
            return StoredExperiment{request.description,context,failed};
        });
        ExperimentFormConfig form;form.comparison=broken;
        check(controller.start(false,form,&input,"small"),"partial delivery start");
        {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return !controller.snapshot().active();}),"partial delivery timeout");}
        check(controller.snapshot().phase==ExecutionPhase::Error && controller.snapshot().latest &&
            !controller.snapshot().error.empty(),"partial evidence not delivered");
        controller.shutdown();
        auto unconfirmed=failed;unconfirmed.study->cleanupConfirmed=false;
        ExperimentExecution blocked([&]{changed.notify_all();},[&](const auto& request,const auto&) {
            return StoredExperiment{request.description,context,unconfirmed};
        });
        check(blocked.start(false,form,&input,"small"),"unconfirmed delivery start");
        {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return !blocked.snapshot().active();}),"unconfirmed delivery timeout");}
        const auto oldId=blocked.snapshot().description.id;
        blocked.start(false,form,&input,"small");
        check(blocked.snapshot().description.id==oldId && blocked.snapshot().error.find("unconfirmed")!=std::string::npos,"unconfirmed cleanup allowed continuation");
        blocked.shutdown();
        // Real worker loss/timeout: no unconfirmed state survives either failure.
        for(int sig:{SIGKILL,SIGSTOP}) {
            auto fault=options;fault.operationTimeout=std::chrono::milliseconds(200);
            mpi::Client client(fault);client.load(input);auto pids=client.workerPids();
            kill(pids.front(),sig);rejects([&]{client.runBatch(3);});
            check(client.cleanupConfirmed(),"batch fault cleanup unconfirmed");
            for(auto pid:pids) check(kill(pid,0)<0,"batch fault residual worker");
        }
        // Wire v1 must fail before reading a payload.
        int fd[2];check(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0,"socketpair");
        mpi::wire::Bytes header;
        for(auto v:{std::uint64_t{0x434f4e5741594d50},std::uint64_t{1},std::uint64_t{mpi::wire::Close},std::uint64_t{42},std::uint64_t{1},std::uint64_t{0}}) mpi::wire::put(header,v);
        send(fd[1],header.data(),header.size(),0);
        rejects([&]{mpi::wire::receive(fd[0],std::chrono::steady_clock::now()+std::chrono::seconds(1));});
        close(fd[0]);close(fd[1]);
        std::cout<<"PASS batch timing/reload, rotating comparison/scaling, warmups, full equivalence, CSV, errors and cleanup\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

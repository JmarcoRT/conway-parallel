#include "app/didactic_session.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace conway;
using namespace conway::app;
using namespace std::chrono_literals;
namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
auto resources() {
    std::vector<std::string> out;for(const auto& p:std::filesystem::directory_iterator("/tmp"))
        if(p.path().filename().string().starts_with("conway-mpi-")) out.push_back(p.path());
    std::sort(out.begin(),out.end());return out;
}
InputData input(std::size_t h) {
    InputData in{5,h,std::vector<std::uint8_t>(5*h)};
    for(std::size_t r=0;r<h;++r) if(r>=1 && r<=3) in.cells[r*5+2]=1;
    return in;
}
void compare(const DidacticController& c,const Game& expected) {
    const auto f=c.snapshot();const auto* actual=c.postStep();
    check(actual && actual->generation()==expected.generation() && actual->aliveCount()==expected.aliveCount(),"MPI generation/population differs from oracle");
    for(std::size_t r=0;r<expected.height();++r) for(std::size_t col=0;col<expected.width();++col)
        check(actual->isAlive(r,col)==expected.isAlive(r,col),"MPI cells differ from oracle");
    check(f.mpi_trace && !c.stepTrace() && f.workers.empty(),"MPI capture replaced by local explanation");
    check(f.stage_duration_ns==2000000000ULL && f.stages.size()==6 && f.total_playback_ns==12000000000ULL,"MPI clock not independent");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==4,"expected launcher, worker, injected-failure worker");const auto baseline=resources();
        mpi::ClientOptions options;options.launcher=argv[1];options.worker=argv[2];options.launcherArguments={"--oversubscribe"};
        options.startupTimeout=5s;options.operationTimeout=2s;
        for(int p:{1,2,4}) {
            options.processes=p;
            for(std::size_t height:{1u,5u,6u}) {
                const auto in=input(height);DidacticController c;
                c.startPreparation(in,Engine::MPI,1,options,0,1);
                check(c.busy() && c.ownsMPISession() && !c.cleanupConfirmed(),"MPI reservation/cleanup state missing before launch");
                c.pause();c.waitUntilReady();check(c.isReady() && !c.isPlaying() && !c.cleanupConfirmed(),"capture completion overrode pause or marked open MPI clean");
                Game oracle(in);oracle.step();compare(c,oracle);
                auto f=c.snapshot();check(f.current_generation==0 && f.captured_generation==1 && f.viewed_generation==0 && f.observed_processes==p,"captured/viewed MPI generation/resources");
                check(f.mpi_trace->ranks.size()==static_cast<std::size_t>(p),"empty ranks omitted");
                for(std::size_t stage=1;stage<6;++stage) {c.stepStage();check(c.snapshot().stage_index==stage,"manual MPI stage jump");}
                check(c.snapshot().viewed_generation==1 && !c.isPlaying(),"result stage/viewed generation wrong");
                c.stepStage();c.waitUntilReady();oracle.step();compare(c,oracle);
                check(c.preStep()->generation()==1 && c.snapshot().stage_index==0 && !c.isPlaying(),"manual MPI transition did not remain paused");
                c.play();
                for(int stage=0;stage<6;++stage) c.tick(2s);
                c.waitUntilReady();oracle.step();compare(c,oracle);
                check(c.isPlaying() && c.preStep()->generation()==2,"automatic MPI transition failed");
                // Reset twice while capture is pending: only latest state can publish.
                for(int stage=0;stage<6;++stage) c.tick(2s);
                c.pause();c.resetToInitial();c.resetToInitial();c.waitUntilReady();
                Game first(in);first.step();compare(c,first);
                check(!c.isPlaying() && c.preStep()->generation()==0,"MPI reset accepted obsolete generation");
                c.close();check(c.phase()==DidacticPhase::Closing && !c.capture() && c.ownsMPISession(),"MPI close is not deferred/reserved");
                c.waitUntilReady();check(c.closed() && c.cleanupConfirmed() && !c.ownsMPISession(),"MPI cleanup not confirmed");
            }
        }
        {
            options.processes=1;options.worker=argv[2];DidacticController c;
            const InputData large{129,129,std::vector<std::uint8_t>(129*129)};
            c.startPreparation(large,Engine::MPI,1,options);c.waitUntilReady();
            check(c.phase()==DidacticPhase::Error && !c.isPlaying() && c.cleanupConfirmed() && !c.ownsMPISession() &&
                c.errorMessage().find("cell limit")!=std::string::npos,"invalid capture policy left a phantom reservation or launched a step");
            c.close();c.waitUntilReady();check(c.closed(),"rejected capture did not close");
        }
        {
            options.processes=2;options.worker=argv[3];DidacticController c;
            c.startPreparation(input(6),Engine::MPI,1,options);c.waitUntilReady();
            const auto diagnostic=c.errorMessage();
            check(c.phase()==DidacticPhase::Error && !c.isPlaying() && !c.capture() && c.cleanupConfirmed() && !c.ownsMPISession(),"post-step failure not stopped/cleaned");
            check(diagnostic.find("injected trace failure after generation 1")!=std::string::npos,"MPI diagnostic lost");
            c.play();c.stepStage();c.tick(100s);check(c.errorMessage()==diagnostic && c.phase()==DidacticPhase::Error,"MPI failure retried implicitly");
            c.close();c.waitUntilReady();check(c.closed() && c.errorMessage()==diagnostic,"close erased diagnostic");
        }
        {
            DidacticController c;options.worker=argv[2];options.processes=1;
            c.startPreparation(input(6),Engine::MPI,1,options);c.close();c.waitUntilReady();
            check(c.closed() && !c.capture() && c.cleanupConfirmed(),"late MPI capture survived immediate close");
        }
        check(resources()==baseline,"MPI controller leaked socket directories");
        std::cout<<"PASS real MPI controller P=1/2/4, uneven/empty ranks, manual/automatic clocks, pending pause/reset/close, errors and cleanup\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

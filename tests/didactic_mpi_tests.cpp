#include "app/didactic_session.hpp"
#include "app/mpi_didactic_view.hpp"
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
    const auto& trace=*f.mpi_trace;
    using namespace didactic_view;
    const auto globalResult=global(trace,DidacticStage::GenerationResult);
    check(globalResult.back().text=="Población global: "+std::to_string(expected.aliveCount()),"view global population not from capture");
    check(globalResult.front().grid && globalResult.front().grid->values.size()==trace.width*trace.height,"missing global grid");
    const auto beforeGlobal=global(trace,DidacticStage::MPIState).front().grid.value();
    for(const auto& r:trace.ranks)for(std::size_t y=0;y<r.partition.rowCount;++y)for(std::size_t x=0;x<trace.width;++x) {
        const auto i=(r.partition.firstRow+y)*trace.width+x;
        check(beforeGlobal.known[i] && beforeGlobal.owners[i]==r.partition.rank && beforeGlobal.values[i]==r.currentBefore[(y+1)*(trace.width+2)+x+1],"global grid changed input/ownership");
    }
    for(std::size_t rank=0;rank<trace.ranks.size();++rank) {
        const auto& r=trace.ranks[rank];
        for(auto stage:{DidacticStage::MPIState,DidacticStage::MPIExchangeFirst,DidacticStage::MPIExchangeSecond,DidacticStage::MPICell,DidacticStage::MPIBuffers,DidacticStage::GenerationResult}) {
            const auto blocks=local(trace,rank,stage),explain=explanation(trace,rank,stage);
            if(r.partition.empty()) {check(!blocks.front().grid && blocks.size()==2,"view invented empty rank work");continue;}
            const auto& current=*blocks[1].grid;
            if(stage==DidacticStage::MPIState || stage==DidacticStage::GenerationResult) {
                for(std::size_t x=0;x<trace.width;++x)check(!current.known[x]&&!current.known[(current.height-1)*current.width+x],"stale halo exposed");
            } else {
                for(std::size_t i=0;i<r.exchanges.size();++i) {
                    const auto& e=r.exchanges[i];
                    const bool known=i<static_cast<std::size_t>(exchangesVisible(stage)) || e.receivedFrom==mpi::noPeer;
                    for(std::size_t x=0;x<trace.width;++x) {
                        const auto index=e.receiveLocalRow*trace.width+x;
                        check(bool(current.known[index])==known,"halo revealed at wrong stage");
                        if(known)check(current.values[index]==(e.receivedFrom==mpi::noPeer?0:e.received[x]) && current.owners[index]==e.receivedFrom,"halo content/source not captured");
                    }
                }
            }
            std::size_t grids=0;for(const auto& b:blocks)grids+=b.grid.has_value();
            check(grids==static_cast<std::size_t>(stage==DidacticStage::MPICell||stage==DidacticStage::MPIBuffers?2:1),"NEXT exposed at wrong stage");
            if(stage==DidacticStage::MPICell) {
                const auto& next=*blocks[3].grid;
                const auto count=std::count(next.known.begin(),next.known.end(),1);
                check(count==(r.cell&&r.cell->captured?1:0),"NEXT reveals unevaluated cells");
                if(r.cell&&r.cell->captured)check(next.values[(r.cell->row-1)*trace.width+r.cell->column-1]==r.cell->evaluation.after,"selected NEXT does not match captured evaluation");
            }
            if(stage==DidacticStage::MPIState || stage==DidacticStage::MPIExchangeFirst || stage==DidacticStage::MPIExchangeSecond) {
                for(const auto* collection:{&blocks,&explain})for(const auto& b:*collection)
                    check(b.text.find("NEXT")==std::string::npos && b.text.find("Swap")==std::string::npos && b.text.find("Regla")==std::string::npos && b.text.find("Resultado generación")==std::string::npos,"presentation leaks future stage");
            }
        }
    }
    for(auto stage:{DidacticStage::MPIExchangeFirst,DidacticStage::MPIExchangeSecond}) {
        const std::size_t call=stage==DidacticStage::MPIExchangeFirst?0:1;
        const auto messages=explanation(trace,0,stage);std::size_t sent=0,shown=0;
        for(const auto& r:trace.ranks)if(!r.partition.empty() && r.exchanges[call].sendTo!=mpi::noPeer) {
            ++sent;const auto& e=r.exchanges[call];
            const auto name="ENVÍA R"+std::to_string(r.partition.rank)+" -> R"+std::to_string(e.sendTo)+" · tag "+std::to_string(e.tag);
            check(std::any_of(messages.begin(),messages.end(),[&](const auto& b){return b.text==name && b.grid && b.grid->values==e.sent;}),"directed message missing or wrong content");
        }
        for(const auto& b:messages)shown+=b.grid.has_value();check(shown==sent,"Sendrecv represented as one global message");
    }
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

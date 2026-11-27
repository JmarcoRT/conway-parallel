#include "app/didactic_session.hpp"
#include "app/mpi_didactic_view.hpp"

#include <iostream>
#include <stdexcept>

using namespace conway;
using namespace conway::app;

namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F operation) {
    try {operation();} catch(const std::exception&) {return;}
    throw std::runtime_error("Invalid hybrid capture accepted");
}
InputData blinker(std::size_t height) {
    InputData input{5,height,std::vector<std::uint8_t>(5*height)};
    for(std::size_t row=1;row<height && row<=3;++row) input.cells[row*5+2]=1;
    return input;
}
void compare(const Game& actual,const Game& reference) {
    check(actual.generation()==reference.generation() && actual.aliveCount()==reference.aliveCount(),
          "Generation or population differs from sequential");
    for(std::size_t row=0;row<reference.height();++row)
        for(std::size_t column=0;column<reference.width();++column)
            check(actual.isAlive(row,column)==reference.isAlive(row,column),"Hybrid cell differs from sequential");
}

void checkSession(mpi::ClientOptions options,std::size_t height,const std::vector<std::size_t>& rows) {
    options.processes=static_cast<int>(rows.size());
    const auto input=blinker(height);
    DidacticController controller;
    controller.startPreparation(input,Engine::MPI,1,options,0,2);
    controller.pause();
    controller.waitUntilReady();
    check(controller.isReady(),controller.errorMessage().c_str());
    Game reference(input);
    for(int generation=0;generation<3;++generation) {
        reference.step();
        const auto capture=controller.capture();
        check(capture && capture->mpiTrace,"Missing hybrid capture");
        compare(capture->after,reference);
        const auto& trace=*capture->mpiTrace;
        mpi::validate_step_trace(trace);
        check(trace.ranks.size()==rows.size(),"Unexpected rank count");
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& rank=trace.ranks[i];
            check(rank.partition.rowCount==rows[i] && rank.team.has_value() && !rank.cell,
                  "Unexpected partition or trace kind");
            check(rank.team->requested==3 && rank.team->effective==2 &&
                  rank.team->workers.size()==std::min<std::size_t>(2,rows[i]),"Team differs from observed work");
            for(std::size_t row=rank.partition.firstRow;row<rank.partition.endRow();++row)
                check(didactic_view::threadForRow(rank,row).has_value(),"Missing global row owner");
            check(!didactic_view::threadForRow(rank,rank.partition.endRow()),"Halo assigned to a thread");
        }
        if(height==5 && generation==0) {
            const auto& down=trace.ranks[0].exchanges[0];
            const auto& up=trace.ranks[1].exchanges[1];
            const std::vector<std::uint8_t> halo{0,0,1,0,0};
            check(down.receivedFrom==1 && down.receivedGlobalRow==3 && down.received==halo &&
                  up.receivedFrom==0 && up.receivedGlobalRow==2 && up.received==halo,"Blinker halos differ");
            auto corrupt=trace;
            corrupt.ranks[0].team->workers.front().first=2;
            rejects([&]{mpi::validate_step_trace(corrupt);});
            corrupt=trace;
            ++corrupt.ranks[0].team->workers.front().end;
            rejects([&]{mpi::validate_step_trace(corrupt);});
            corrupt=trace;
            ++corrupt.ranks[0].team->workers.back().end;
            rejects([&]{mpi::validate_step_trace(corrupt);});
        }
        for(std::size_t stage=1;stage<6;++stage) {
            controller.stepStage();
            const auto frame=controller.snapshot();
            check(frame.stage_index==stage && controller.capture()==capture,"Stage changed the capture");
            if(stage==3) check(frame.stages[stage]==DidacticStage::MPIThreadWork,"Missing OpenMP stage");
        }
        controller.stepStage();
        controller.waitUntilReady();
        check(controller.isReady(),controller.errorMessage().c_str());
    }
    controller.resetToInitial();
    controller.waitUntilReady();
    const auto frame=controller.snapshot();
    check(frame.viewed_generation==0 && frame.stage_index==0 && !frame.playing && frame.requested_threads==3,
          "Reset lost session resources or stage");
    controller.close();
    controller.waitUntilReady();
    check(controller.closed() && controller.cleanupConfirmed(),"Worker cleanup unconfirmed");
}
}

int main(int argc,char** argv) {
    try {
        check(argc==3,"Expected launcher and worker");
        mpi::ClientOptions options;
        options.launcher=argv[1];options.worker=argv[2];options.openMPThreads=3;
        checkSession(options,5,{3,2});
        checkSession(options,7,{3,2,2});
        checkSession(options,1,{1,0});
        // A missing launcher makes accidental execution visible in the diagnostic.
        options.launcher="/no/such/launcher";
        DidacticController rejected;
        rejected.startPreparation({129,129,std::vector<std::uint8_t>(129*129)},Engine::MPI,1,options);
        rejected.waitUntilReady();
        check(rejected.phase()==DidacticPhase::Error && rejected.cleanupConfirmed() &&
              rejected.errorMessage().find("cell limit")!=std::string::npos,"Capture limit was not checked before launch");
        std::cout<<"PASS hybrid capture, bidirectional blinker halos, unequal/empty ranks, observed teams and reset\n";
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}

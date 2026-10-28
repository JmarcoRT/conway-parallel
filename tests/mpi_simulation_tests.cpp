#include "app/simulation.hpp"
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace conway;
using namespace conway::app;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void ready(SimulationSession& session) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do {session.poll(); if(!session.busy()) return; std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("session timeout");
}
bool equal(const Game& a,const Game& b) {
    if(a.width()!=b.width() || a.height()!=b.height() || a.generation()!=b.generation() || a.aliveCount()!=b.aliveCount()) return false;
    for(std::size_t r=0;r<a.height();++r) for(std::size_t c=0;c<a.width();++c)
        if(a.isAlive(r,c)!=b.isAlive(r,c)) return false;
    return true;
}
void gone(const std::vector<int>& pids) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    for(;;) {
        bool live=false; for(auto pid:pids) live=live || kill(pid,0)==0;
        if(!live) return;
        if(std::chrono::steady_clock::now()>deadline) throw std::runtime_error("residual MPI worker");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"expected launcher and worker");
        check(parseMPIProcesses("1")==1 && parseMPIProcesses("1024")==1024 &&
              !parseMPIProcesses("0") && !parseMPIProcesses("1025") && !parseMPIProcesses("-1") &&
              !parseMPIProcesses("2x"),"MPI process count validation");
        InputData input{5,5,std::vector<std::uint8_t>(25)};
        input.cells[11]=input.cells[12]=input.cells[13]=1;
        auto options=defaultMPIOptions(2); options.launcher=argv[1]; options.worker=argv[2];
        options.launcherArguments={"--oversubscribe"}; // Test policy, never application default.
        options.operationTimeout=std::chrono::milliseconds(250);
        const auto uiThread=std::this_thread::get_id(); std::atomic<bool> workerNotify{false};
        SimulationSession session(input,Engine::MPI,options,[&]{workerNotify=std::this_thread::get_id()!=uiThread;});
        ready(session); check(session.error().empty(),session.error().c_str());
        check(workerNotify && !session.preview(),"MPI notification/preview contract");
        Game reference(input);
        session.step();
        for(int i=0;i<100;++i) {session.step();session.advanceIfRunning();}
        ready(session); reference.step(); check(equal(session.game(),reference),"steps accumulated while in flight");
        session.toggleRunning(); session.advanceIfRunning(); session.pause();
        ready(session); reference.step(); check(equal(session.game(),reference) && !session.isRunning(),"pause lost pending step");
        session.advanceIfRunning(); check(equal(session.game(),reference),"paused session advanced");
        auto old=session.mpiWorkerPids();
        session.step(); session.configure(Engine::MPI,4);
        ready(session); reference.step();
        check(equal(session.game(),reference) && session.mpiWorkerPids().size()==4,"P change lost pending state"); gone(old);
        old=session.mpiWorkerPids();
        session.configure(Engine::Sequential,4); ready(session); gone(old);
        check(equal(session.game(),reference),"MPI to sequential lost state");
        session.step(); reference.step(); check(equal(session.game(),reference),"sequential regression");
        session.configure(Engine::OpenMP,4); session.step(); reference.step(); check(equal(session.game(),reference),"OpenMP regression");
        session.configure(Engine::MPI,2); ready(session); check(equal(session.game(),reference),"return to MPI lost state");
        session.step(); session.reset(); ready(session); reference=Game(input);
        check(equal(session.game(),reference) && !session.isRunning(),"reset baseline lost across engines/P");
        for(bool timeout:{false,true}) {
            auto confirmed=session.game(); old=session.mpiWorkerPids();
            check(kill(old.front(),timeout?SIGSTOP:SIGKILL)==0,"failure injection failed");
            const auto start=std::chrono::steady_clock::now();
            session.toggleRunning(); session.step(); session.pause();
            check(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100),"UI blocked on client operation");
            ready(session);
            check(!session.error().empty() && !session.isRunning() && equal(session.game(),confirmed),"failure lost confirmed state"); gone(old);
            session.recover(); ready(session); check(session.error().empty() && equal(session.game(),confirmed),"recovery lost state");
        }
        old=session.mpiWorkerPids(); session.step(); session.close(); ready(session);
        check(session.closed(),"close did not complete after pending advance"); gone(old);
        session.close(); check(session.closed() && !session.busy(),"close is not idempotent");
        {
            SimulationSession idleFailure(input,Engine::MPI,options); ready(idleFailure);
            auto pids=idleFailure.mpiWorkerPids();
            kill(pids.front(),SIGKILL);
            idleFailure.configure(Engine::OpenMP,2); ready(idleFailure);
            check(idleFailure.error().empty(),"dead idle MPI prevented local fallback");
            idleFailure.step(); check(idleFailure.game().generation()==1,"local fallback did not advance");
            gone(pids); idleFailure.close(); ready(idleFailure);
        }
        auto invalid=options; invalid.launcher="/no/such/mpi-launcher";
        SimulationSession failed(input,Engine::MPI,invalid); ready(failed);
        check(!failed.error().empty() && failed.game().generation()==0,"launch failure lost board");
        failed.configure(Engine::Sequential,1); ready(failed); failed.step();
        check(failed.error().empty() && failed.game().generation()==1,"launch failure blocked local fallback");
        failed.close(); ready(failed);
        std::cout<<"PASS async controls, no backlog, pause, engine/P transfer, reset, real faults/recovery, cleanup, local regressions\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

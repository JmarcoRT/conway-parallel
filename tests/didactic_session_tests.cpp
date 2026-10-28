#include "app/didactic_session.hpp"
#include <omp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>

using namespace conway;
using namespace conway::app;
using namespace std::chrono_literals;
namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
template<class F> void rejects(F&& f) {bool failed=false;try {f();} catch(const std::exception&) {failed=true;}check(failed,"expected rejection");}
InputData input(std::size_t h=6) {
    InputData in{5,h,std::vector<std::uint8_t>(5*h)};
    for(std::size_t i=0;i<in.cells.size();++i) in.cells[i]=(i*17+3)%13<4;
    return in;
}
bool same(const Game& a,const Game& b) {
    if(a.width()!=b.width() || a.height()!=b.height() || a.generation()!=b.generation() || a.aliveCount()!=b.aliveCount()) return false;
    for(std::size_t r=0;r<a.height();++r) for(std::size_t c=0;c<a.width();++c) if(a.isAlive(r,c)!=b.isAlive(r,c)) return false;
    return true;
}
class Gate final:public DidacticBackend {
    std::unique_ptr<DidacticBackend> real_=makeDidacticBackend();
    std::mutex mutex_;
    std::condition_variable cv_;
    int permitted_=0,closesPermitted_=1000,calls_=0,closes_=0,fail_=0;
    bool uncertain_=false,uncleanFailure_=false;
public:
    void permit(int n) {{std::lock_guard lock(mutex_);permitted_=n;}cv_.notify_all();}
    void permitClose(int n) {{std::lock_guard lock(mutex_);closesPermitted_=n;}cv_.notify_all();}
    void fail(int n,bool unclean=false) {std::lock_guard lock(mutex_);fail_=n;uncleanFailure_=unclean;}
    void entered(int n,bool close=false) {
        std::unique_lock lock(mutex_);
        check(cv_.wait_for(lock,2s,[&]{return (close?closes_:calls_)>=n;}),"worker never entered gate");
    }
    int calls() {std::lock_guard lock(mutex_);return calls_;}
    std::shared_ptr<const DidacticCapture> capture(const DidacticRequest& q) override {
        {std::unique_lock lock(mutex_);const int call=++calls_;cv_.notify_all();cv_.wait(lock,[&]{return permitted_>=call;});
         if(call==fail_) {uncertain_=uncleanFailure_;throw std::runtime_error("injected capture diagnostic");}}
        return real_->capture(q);
    }
    void close() override {
        {std::unique_lock lock(mutex_);const int close=++closes_;cv_.notify_all();cv_.wait(lock,[&]{return closesPermitted_>=close;});}
        real_->close();if(uncertain_) throw std::runtime_error("injected unconfirmed cleanup");
    }
    bool ownsMPI() const noexcept override {return uncertain_ || real_->ownsMPI();}
    bool cleanupConfirmed() const noexcept override {return !uncertain_ && real_->cleanupConfirmed();}
};
struct Release {Gate& gate;~Release(){gate.permit(1000);gate.permitClose(1000);}};
void localRegressions() {
    for(auto engine:{Engine::Sequential,Engine::OpenMP}) for(int threads:{1,2,4,8}) {
        const auto in=input(5);DidacticController c;
        c.prepareSynchronously(in,engine,threads);
        auto f=c.snapshot();Game oracle(in);oracle.step();
        check(c.isReady() && c.isPlaying() && same(*c.postStep(),oracle),"local capture/automatic initial playback incorrect");
        check(f.stages==std::vector<DidacticStage>{DidacticStage::LocalWork,DidacticStage::GenerationResult},"invented local trace events");
        check(f.stage_duration_ns==2000000000ULL && f.total_playback_ns==4000000000ULL && f.stage_elapsed_ns==0,"clock depends on compute time");
        check(f.viewed_generation==0 && f.captured_generation==1,"viewed/captured generation conflated");
        std::size_t coverage=0;for(const auto& w:f.workers) {coverage+=w.end_row-w.first_row;check(w.progress==0,"worker starts revealed");}
        check(coverage==in.height && c.stepTrace() && !f.mpi_trace && c.stepTrace()->requested_threads==threads,"local observed coverage/trace regression");
        c.tick(1s);f=c.snapshot();check(f.stage_index==0 && f.stage_elapsed_ns==1000000000ULL,"independent slow tick");
        c.pause();const auto paused=f.current_playback_ns;c.tick(100s);check(c.snapshot().current_playback_ns==paused,"pause advanced clock");
        c.stepStage();f=c.snapshot();check(f.stage_index==1 && f.fully_revealed && f.viewed_generation==1 && !c.isPlaying(),"manual stage skipped/started playback");
        check(std::all_of(f.revealed.begin(),f.revealed.end(),[](auto v){return v==1;}),"recorded local output not revealed");
        c.stepStage();c.waitUntilReady();check(c.preStep()->generation()==1 && c.postStep()->generation()==2 && !c.isPlaying(),"manual next capture lost pause");
        oracle.step();check(same(*c.postStep(),oracle),"local generation 2 regression");
        c.resetToInitial();c.waitUntilReady();check(c.preStep()->generation()==0 && c.postStep()->generation()==1 && !c.isPlaying(),"reset not initial/paused");
        c.close();c.waitUntilReady();check(c.closed() && !c.capture(),"local async close");
    }
    std::cout<<"PASS sequential/OpenMP captures, coverage, equivalence, stage contract and paused reset (1/2/4/8 threads)\n";
}
void clockTests() {
    DidacticController c;c.prepareSynchronously(input(),Engine::Sequential,1);
    c.tick(500ms);const auto before=c.snapshot();c.setSpeed(4);
    check(c.snapshot().stage_elapsed_ns==before.stage_elapsed_ns && c.snapshot().stage_index==0,"changing speed moves stage");
    c.tick(250ms);check(c.snapshot().stage_elapsed_ns==1500000000ULL,"speed scale incorrect");
    c.tick(125ms);check(c.snapshot().stage_index==1 && c.snapshot().stage_elapsed_ns==0,"exact stage boundary incorrect");
    c.tick(100s);c.waitUntilReady();
    check(c.preStep()->generation()==1 && c.snapshot().stage_index==0 && c.isPlaying(),"automatic generation transition/overshoot");
    c.tick(100s);check(c.snapshot().stage_index==1 && c.preStep()->generation()==1,"tick skipped stages/generations");
    c.setSpeed(std::numeric_limits<double>::quiet_NaN());check(c.speed()==4,"NaN speed accepted");
    c.setSpeed(100);check(c.speed()==8,"speed upper bound");c.decreaseSpeed();check(c.speed()==4,"decrease speed");
    c.setSpeed(.25);c.increaseSpeed();check(c.speed()==.5,"increase speed");
    c.pause();c.tick(-1s);check(c.snapshot().stage_elapsed_ns==0,"negative tick");
    rejects([&]{c.setStageDuration(0ns);});c.setStageDuration(3s);check(c.snapshot().stage_duration_ns==3000000000ULL,"adjustable stage duration");
    c.close();c.waitUntilReady();
    std::cout<<"PASS simulated clock, automatic transition, speed changes without stage jumps, overshoot bounded\n";
}
void pauseDuringCapture() {
    auto gate=std::make_unique<Gate>();auto* g=gate.get();std::atomic<int> notices=0;
    DidacticController c([&]{++notices;},std::move(gate));Release release{*g};
    c.startPreparation(input(),Engine::OpenMP,2);g->entered(1);c.pause();c.stepStage();
    check(c.busy() && !c.isPlaying() && !c.snapshot().has_data,"pause during capture ignored");
    g->permit(1);c.waitUntilReady();check(!c.isPlaying() && c.snapshot().stage_elapsed_ns==0,"late capture resumed animation");
    c.play();c.tick(2s);c.tick(2s);g->entered(2);
    for(int i=0;i<10;++i) {c.tick(100s);c.stepStage();}
    check(g->calls()==2 && c.busy(),"accumulated capture requests while pending");
    c.pause();g->permit(2);c.waitUntilReady();check(!c.isPlaying() && c.preStep()->generation()==1 && c.snapshot().stage_index==0,"pause lost across automatic boundary");
    c.close();c.waitUntilReady();check(notices>=2,"capture completions did not notify UI");
    std::cout<<"PASS pause during capture/automatic transition, no queued captures, late delivery stays paused\n";
}
void coalescedReset() {
    auto gate=std::make_unique<Gate>();auto* g=gate.get();std::mutex mutex;std::condition_variable cv;int notices=0;
    DidacticController c([&]{{std::lock_guard lock(mutex);++notices;}cv.notify_all();},std::move(gate));Release release{*g};
    c.startPreparation(input(),Engine::Sequential,1);g->entered(1);
    c.startPreparation(input(3),Engine::Sequential,1);c.pause();c.resetToInitial();c.resetToInitial();
    g->permit(1);
    {std::unique_lock lock(mutex);check(cv.wait_for(lock,2s,[&]{return notices>=1;}),"old delivery missing");}
    c.poll();g->entered(2);
    check(!c.capture() && c.isPreparing() && !c.isPlaying(),"obsolete result was accepted while reset pending");
    g->permit(2);c.waitUntilReady();
    check(g->calls()==2 && c.preStep()->generation()==0 && c.preStep()->height()==3 && !c.isPlaying(),"reset was queued twice or accepted old input");
    g->permit(100);c.startPreparation(input(3),Engine::Sequential,1);c.close();c.waitUntilReady();
    check(c.closed() && !c.snapshot().has_data && !c.isPlaying(),"close accepted a late restarted capture");
    std::cout<<"PASS superseded reset/start/close deliveries and reset coalescing\n";
}
void gatedClose() {
    auto gate=std::make_unique<Gate>();auto* g=gate.get();std::mutex mutex;std::condition_variable cv;int notifications=0;
    DidacticController c([&]{{std::lock_guard lock(mutex);++notifications;}cv.notify_all();},std::move(gate));Release release{*g};
    c.startPreparation(input(),Engine::Sequential,1);g->entered(1);g->permitClose(0);
    c.resetToInitial();c.close();g->permit(1);
    {std::unique_lock lock(mutex);check(cv.wait_for(lock,2s,[&]{return notifications>=1;}),"old capture did not finish");}
    c.poll();g->entered(1,true);
    check(c.phase()==DidacticPhase::Closing && c.busy() && !c.snapshot().has_data,"close blocked UI/published stale data");
    c.close();check(g->calls()==1,"close ran a superseded reset");
    g->permitClose(1);c.waitUntilReady();check(c.closed() && !c.capture(),"close did not complete");
    std::cout<<"PASS nonblocking close during capture, obsolete delivery discarded and queued reset cancelled\n";
}
void errors() {
    DidacticController invalid;invalid.startPreparation({0,0,{}},Engine::Sequential,1);invalid.waitUntilReady();
    check(invalid.phase()==DidacticPhase::Error && !invalid.isPlaying() && !invalid.errorMessage().empty(),"initial error hidden or playing");
    const auto message=invalid.errorMessage();invalid.play();invalid.stepStage();invalid.tick(10s);
    check(invalid.errorMessage()==message && !invalid.isPlaying(),"error retried automatically or lost diagnosis");
    invalid.close();invalid.waitUntilReady();check(invalid.closed() && invalid.errorMessage()==message,"close lost failure diagnostic");
    {
        auto gate=std::make_unique<Gate>();auto* g=gate.get();DidacticController c({},std::move(gate));Release release{*g};
        g->permit(100);c.startPreparation(input(),Engine::Sequential,1);c.waitUntilReady();g->fail(2);
        c.tick(2s);c.tick(2s);c.waitUntilReady();
        check(c.phase()==DidacticPhase::Error && !c.isPlaying() && c.capture() && c.errorMessage().find("injected capture diagnostic")!=std::string::npos,"next-generation failure lost last accepted capture/diagnostic");
        c.play();c.stepStage();c.tick(100s);check(g->calls()==2,"failed capture retried");
        c.resetToInitial();c.waitUntilReady();check(c.isReady() && c.errorMessage().empty() && !c.isPlaying(),"explicit recovery/reset failed");
        c.close();c.waitUntilReady();
    }
    {
        auto gate=std::make_unique<Gate>();auto* g=gate.get();DidacticController c({},std::move(gate));Release release{*g};
        c.startPreparation(input(),Engine::Sequential,1);g->entered(1);g->fail(1,true);c.resetToInitial();g->permit(100);
        c.waitUntilReady();check(c.phase()==DidacticPhase::Error && c.ownsMPISession() && !c.cleanupConfirmed() && g->calls()==1,"obsolete failure hid unconfirmed cleanup or started replacement");
        c.close();c.waitUntilReady();check(!c.closed() && c.ownsMPISession() && c.errorMessage().find("injected capture diagnostic")!=std::string::npos,"uncertain close unblocked resources or erased original diagnostic");
    }
    std::cout<<"PASS initial/next/stale errors, diagnosis retention, explicit recovery and uncertain-cleanup protection\n";
}
}
int main() {
    try {omp_set_dynamic(0);localRegressions();clockTests();pauseDuringCapture();coalescedReset();gatedClose();errors();}
    catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

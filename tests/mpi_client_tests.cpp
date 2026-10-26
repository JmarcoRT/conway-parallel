#include "mpi/client.hpp"
#include "mpi/local_state.hpp"
#include "core/engines/sequential.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

extern char** environ;

namespace {
using namespace conway;
using namespace conway::mpi;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> std::string fails(F&& f) {
    try { f(); } catch(const std::exception& e) { return e.what(); }
    throw std::runtime_error("expected failure did not occur");
}
auto resources() {
    std::vector<std::string> paths;
    for(const auto& p:std::filesystem::directory_iterator("/tmp"))
        if(p.path().filename().string().starts_with("conway-mpi-")) paths.push_back(p.path());
    std::sort(paths.begin(),paths.end()); return paths;
}
void gone(const std::vector<int>& workers,int launcher,const std::string& dir) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    bool live;
    do {
        live=kill(-launcher,0)==0;
        for(auto pid:workers) live=live || kill(pid,0)==0;
        if(!live) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while(std::chrono::steady_clock::now()<deadline);
    check(!live,"residual MPI process or process group");
    check(!std::filesystem::exists(dir),"residual socket directory");
}
void protocol_checks() {
    int sockets[2]; check(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets)==0,"socketpair failed");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    wire::Bytes bytes;
    for(auto v:{std::uint64_t{0x434f4e5741594d50},wire::version,std::uint64_t{wire::Error},
                std::uint64_t{42},std::uint64_t{7},std::uint64_t{3}}) wire::put(bytes,v);
    bytes.insert(bytes.end(),{0,1,0});
    std::thread fragmented([&]{for(auto byte:bytes) ::send(sockets[1],&byte,1,MSG_NOSIGNAL);});
    const auto frame=wire::receive(sockets[0],deadline);
    fragmented.join();
    check(frame.session==42 && frame.request==7 && frame.payload==wire::Bytes({0,1,0}),"fragmented frame corrupted");
    wire::Bytes oversized;
    for(auto v:{std::uint64_t{0x434f4e5741594d50},wire::version,std::uint64_t{wire::Reply},
                std::uint64_t{42},std::uint64_t{8},wire::maxPayload+1}) wire::put(oversized,v);
    ::send(sockets[1],oversized.data(),oversized.size(),MSG_NOSIGNAL);
    check(fails([&]{wire::receive(sockets[0],deadline);}).find("too large")!=std::string::npos,
          "oversized payload accepted");
    ::close(sockets[1]);
    check(fails([&]{wire::receive(sockets[0],deadline);}).find("disconnected")!=std::string::npos,
          "EOF was not detected");
    ::close(sockets[0]);
    std::cout<<"PASS fragmented frames, payload limit, EOF\n";

    check(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets)==0,"socketpair failed");
    const auto started=std::chrono::steady_clock::now();
    std::jthread trickle([&](std::stop_token stop){
        for(auto byte:bytes) {
            if(stop.stop_requested()) break;
            ::send(sockets[1],&byte,1,MSG_NOSIGNAL);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
    check(fails([&]{wire::receive(sockets[0],started+std::chrono::milliseconds(90));}).find("timeout")!=std::string::npos,
          "trickle reset read deadline");
    trickle.request_stop(); trickle.join();
    check(std::chrono::steady_clock::now()-started<std::chrono::seconds(1),"read deadline unbounded");
    ::close(sockets[0]); ::close(sockets[1]);

    check(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets)==0,"socketpair failed");
    int small=1024; setsockopt(sockets[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof(small));
    wire::Frame large{wire::Error,42,1,wire::Bytes(1024*1024,'x')};
    std::jthread slowReader([&](std::stop_token stop){
        char buffer[1024];
        while(!stop.stop_requested()) {
            recv(sockets[1],buffer,sizeof(buffer),MSG_DONTWAIT);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    check(fails([&]{wire::send(sockets[0],large,std::chrono::steady_clock::now()+std::chrono::milliseconds(90));})
          .find("timeout")!=std::string::npos,"blocked partial write had no deadline");
    slowReader.request_stop(); slowReader.join();
    ::close(sockets[0]); ::close(sockets[1]);
    std::cout<<"PASS absolute deadlines under trickle reads and blocked partial writes\n";
}

// Raw peer for the REAL MPI worker: bypasses Client's preflight deliberately.
struct RawWorker {
    int launcher{-1},listener{-1},fd{-1};
    std::string dir,path;
    std::vector<int> pids;
    RawWorker(const char* executable,const char* worker) {
        char temp[]="/tmp/conway-raw-test-XXXXXX";
        auto created=mkdtemp(temp); check(created,"raw mkdtemp failed"); dir=created; path=dir+"/control";
        listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        std::copy(path.begin(),path.end(),address.sun_path);
        check(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0 && listen(listener,1)==0,"raw bind failed");
        std::vector<std::string> args{executable,"--oversubscribe","--host","localhost","-np","2",worker,
                                      "--socket",path,"--session","42"};
        std::vector<char*> argv; for(auto& a:args) argv.push_back(a.data()); argv.push_back(nullptr);
        posix_spawnattr_t attr; posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&attr,0);
        const auto error=posix_spawnp(&launcher,argv[0],nullptr,&attr,argv.data(),environ);
        posix_spawnattr_destroy(&attr); check(error==0,"raw spawn failed");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(fd<0 && std::chrono::steady_clock::now()<deadline) {
            fd=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);
            if(fd<0) { pollfd p{listener,POLLIN,0}; poll(&p,1,10); }
        }
        check(fd>=0,"raw worker handshake timeout");
        auto ready=wire::receive(fd,deadline);
        std::size_t offset=0; check(wire::get(ready.payload,offset)==2,"raw rank count");
        for(int i=0;i<2;++i) pids.push_back(wire::get(ready.payload,offset));
    }
    ~RawWorker() {
        if(fd>=0) ::close(fd);
        if(listener>=0) ::close(listener);
        if(launcher>0) {
            // Give EOF a chance to finalize real ranks before escalation.
            for(int i=0;i<50;++i) {
                siginfo_t info{};
                if(waitid(P_PID,launcher,&info,WEXITED|WNOHANG|WNOWAIT)==0 && info.si_pid) break;
                poll(nullptr,0,10);
            }
            kill(-launcher,SIGTERM); poll(nullptr,0,50); kill(-launcher,SIGKILL);
            waitpid(launcher,nullptr,0);
        }
        unlink(path.c_str()); rmdir(dir.c_str());
    }
    auto deadline() { return std::chrono::steady_clock::now()+std::chrono::seconds(2); }
    auto request(const wire::Frame& frame) { wire::send(fd,frame,deadline()); return wire::receive(fd,deadline()); }
};
void real_protocol_checks(const char* launcher,const char* worker) {
    for(int invalid=0;invalid<3;++invalid) {
        std::vector<int> pids; int pid; std::string dir;
        {
            RawWorker real(launcher,worker); pids=real.pids; pid=real.launcher; dir=real.dir;
            wire::Bytes header;
            for(auto v:{std::uint64_t{0x434f4e5741594d50},wire::version,
                        invalid==0?std::uint64_t{999}:std::uint64_t{wire::Load},
                        invalid==1?std::uint64_t{43}:std::uint64_t{42},std::uint64_t{1},std::uint64_t{1024*1024}})
                wire::put(header,v);
            if(invalid==2) { wire::put(header,1); wire::put(header,1); wire::put(header,0); }
            ::send(real.fd,header.data(),header.size(),MSG_NOSIGNAL);
            check(fails([&]{wire::receive(real.fd,real.deadline());}).find("disconnected")!=std::string::npos,
                  "real worker waited for body of invalid header/metadata");
        }
        gone(pids,pid,dir);
    }
    {
        RawWorker real(launcher,worker);
        wire::Bytes board; wire::put(board,3); wire::put(board,1); wire::put(board,7);
        board.insert(board.end(),{1,1,1});
        check(real.request({wire::Load,42,1,board}).type==wire::Reply,"raw load failed");
        board.back()=2;
        check(real.request({wire::Load,42,2,board}).type==wire::Error,"invalid cells accepted");
        auto snapshot=real.request({wire::Snapshot,42,3,{}});
        std::size_t offset=0;
        check(wire::get(snapshot.payload,offset)==3 && wire::get(snapshot.payload,offset)==1 &&
              wire::get(snapshot.payload,offset)==7 && wire::get(snapshot.payload,offset)==3 &&
              wire::Bytes(snapshot.payload.begin()+offset,snapshot.payload.end())==wire::Bytes({1,1,1}),
              "invalid LOAD modified real worker state");
        check(real.request({wire::Close,42,4,{}}).type==wire::Closed,"raw coordinated close failed");
    }
    std::cout<<"PASS real MPI worker rejects invalid header/IDs/metadata/cells before mutation\n";
}
}
int main(int argc,char** argv) {
    // A launcher that never connects, used to exercise startup timeout. It
    // writes diagnostics too, proving the client drains its pipe while waiting.
    if(argc>1 && std::string(argv[1])=="--hang") {
        std::ofstream(argv[2])<<getpid()<<std::endl;
        for(;;) { std::cerr<<std::string(2048,'x'); usleep(1000); }
    }
    if(argc>1 && (std::string(argv[1])=="--wrong-id" || std::string(argv[1])=="--forged-pid")) {
        const bool forged=std::string(argv[1])=="--forged-pid";
        std::string path; std::uint64_t session=0;
        for(int i=2;i+1<argc;++i) {
            if(std::string(argv[i])=="--socket") path=argv[i+1];
            if(std::string(argv[i])=="--session") session=std::stoull(argv[i+1]);
        }
        const int fd=socket(AF_UNIX,SOCK_STREAM,0);
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        std::copy(path.begin(),path.end(),address.sun_path);
        if(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))) return 2;
        wire::Bytes payload; wire::put(payload,forged?2:1); wire::put(payload,getpid());
        if(forged) wire::put(payload,std::stoull(argv[2]));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        wire::send(fd,{wire::Ready,session,0,payload},deadline);
        const auto request=wire::receive(fd,deadline);
        wire::send(fd,{wire::Reply,session,request.request+1,{}},deadline);
        for(;;) pause();
    }
    try {
        check(argc==4,"expected launcher, worker, repository");
        protocol_checks();
        real_protocol_checks(argv[1],argv[2]);
        ClientOptions options;
        options.launcher=argv[1]; options.worker=argv[2]; options.processes=2;
        // Local test policy, deliberately outside Client's defaults.
        options.launcherArguments={"--oversubscribe"};
        options.startupTimeout=std::chrono::seconds(5);
        options.operationTimeout=std::chrono::seconds(1);
        const auto baseline=resources();
        for(int count:{1,2,4}) {
            options.processes=count;
            Client client(options);
            const auto pids=client.workerPids(); const auto launcher=client.launcherPid();
            const auto dir=client.resourceDirectory();
            const auto input=loadInput(std::string(argv[3])+"/patterns/blinker.txt");
            auto reference=make_local_state(input,partition_rows(input.height,1,0));
            client.load(input);
            for(std::size_t generation=0;generation<=8;++generation) {
                if(generation) { client.advance(); core::step_sequential(reference); }
                const auto snapshot=client.snapshot();
                check(snapshot.input.width==input.width && snapshot.input.height==input.height &&
                      snapshot.input.cells==pack_local_rows(reference) &&
                      snapshot.generation==reference.generation && snapshot.aliveCount==reference.aliveCount,
                      "snapshot differs from sequential");
            }
            // Reload on the same job and preserve a nonzero starting generation.
            const InputData tiny{3,1,{1,1,1}};
            client.load(tiny,12); client.advance(3);
            auto ref=make_local_state(tiny,partition_rows(1,1,0)); ref.generation=12;
            for(int i=0;i<3;++i) core::step_sequential(ref);
            auto snapshot=client.snapshot();
            check(snapshot.input.cells==pack_local_rows(ref) && snapshot.generation==15 &&
                  snapshot.aliveCount==ref.aliveCount,"reload or empty ranks failed");
            client.close(); client.close(); gone(pids,launcher,dir);
        }
        std::cout<<"PASS load/advance/snapshot/reload/close P=1,2,4\n";

        auto broken=options; broken.launcher="/nonexistent/conway-launcher";
        fails([&]{Client client(broken);});
        broken=options; broken.worker="/nonexistent/conway-worker";
        fails([&]{Client client(broken);});
        check(resources()==baseline,"launch failure leaked resources");
        std::cout<<"PASS launch failures\n";

        broken=options; broken.launcher=std::filesystem::canonical(argv[0]);
        char pidPath[]="/tmp/conway-client-test-XXXXXX";
        const int pidFile=mkstemp(pidPath); check(pidFile>=0,"test pid file failed"); ::close(pidFile);
        broken.launcherArguments={"--hang",pidPath}; broken.startupTimeout=std::chrono::milliseconds(150);
        check(fails([&]{Client client(broken);}).find("timeout")!=std::string::npos,"expected startup timeout");
        int fakePid=0; std::ifstream(pidPath)>>fakePid; unlink(pidPath);
        check(fakePid>0 && kill(fakePid,0)<0,"startup timeout left launcher alive");
        check(resources()==baseline,"startup timeout leaked resources");
        std::cout<<"PASS startup timeout with noisy launcher\n";

        broken=options; broken.processes=1;
        broken.launcher=std::filesystem::canonical(argv[0]); broken.launcherArguments={"--wrong-id"};
        {
            Client client(broken);
            const auto pids=client.workerPids(); const auto launcher=client.launcherPid();
            const auto dir=client.resourceDirectory();
            check(fails([&]{client.load(InputData{1,1,{1}});}).find("ID mismatch")!=std::string::npos,
                  "wrong response ID accepted");
            gone(pids,launcher,dir);
        }
        std::cout<<"PASS response ID validation and cleanup\n";

        int outsider=-1;
        char sleepPath[]="/bin/sleep",sleepTime[]="10";
        char* sleepArgs[]{sleepPath,sleepTime,nullptr};
        check(posix_spawnp(&outsider,sleepPath,nullptr,nullptr,sleepArgs,environ)==0,"outsider spawn failed");
        broken.processes=2;
        broken.launcherArguments={"--forged-pid",std::to_string(outsider)};
        const auto forgedError=fails([&]{Client client(broken);});
        const bool outsiderAlive=kill(outsider,0)==0;
        kill(outsider,SIGTERM); waitpid(outsider,nullptr,0);
        check(forgedError.find("descendant")!=std::string::npos && outsiderAlive,"forged PID affected unrelated process");
        std::cout<<"PASS partial handshake rejects unrelated PID without signaling it (double)\n";

        options.processes=1;
        std::vector<int> destructorPids; int destructorLauncher; std::string destructorDir;
        {
            Client client(options);
            destructorPids=client.workerPids(); destructorLauncher=client.launcherPid(); destructorDir=client.resourceDirectory();
            client.load(InputData{1,1,{1}});
        }
        gone(destructorPids,destructorLauncher,destructorDir);
        std::cout<<"PASS destructor closes real MPI session\n";
        std::string reused;
        {
            Client client(options); reused=client.resourceDirectory(); client.close();
            std::filesystem::create_directory(reused);
            std::ofstream(reused+"/control")<<"new owner";
            client.close();
        }
        check(std::filesystem::exists(reused+"/control"),"destructor touched a released resource name");
        unlink((reused+"/control").c_str()); rmdir(reused.c_str());
        std::cout<<"PASS repeated cleanup does not touch reused resource names\n";

        options.processes=2;
        for(bool timeout:{false,true}) {
            Client client(options);
            client.load(loadInput(std::string(argv[3])+"/patterns/glider.txt"));
            const auto pids=client.workerPids(); const auto launcher=client.launcherPid();
            const auto dir=client.resourceDirectory();
            check(kill(pids.front(),timeout?SIGSTOP:SIGKILL)==0,"inject worker failure failed");
            const auto error=fails([&]{client.snapshot();});
            if(timeout) check(error.find("timeout")!=std::string::npos,"expected operation timeout");
            gone(pids,launcher,dir);
            std::cout<<"PASS "<<(timeout?"operation timeout":"worker disconnect")<<" and cleanup\n";
        }
        {
            Client survivor(options);
            survivor.load(InputData{1,1,{1}});
            Client failed(options);
            failed.load(InputData{1,1,{1}});
            const auto pids=failed.workerPids(); const auto launcher=failed.launcherPid();
            const auto dir=failed.resourceDirectory();
            check(kill(launcher,SIGKILL)==0,"launcher kill failed");
            // Wait for actual exit, without reaping: kill() alone only queues
            // the signal and a final snapshot can otherwise race with delivery.
            const auto exitedBy=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            siginfo_t exited{};
            do {
                waitid(P_PID,launcher,&exited,WEXITED|WNOHANG|WNOWAIT);
                if(!exited.si_pid) poll(nullptr,0,5);
            } while(!exited.si_pid && std::chrono::steady_clock::now()<exitedBy);
            check(exited.si_pid==launcher,"launcher did not exit");
            fails([&]{failed.snapshot();});
            gone(pids,launcher,dir);
            survivor.advance();
            const auto snapshot=survivor.snapshot();
            check(snapshot.generation==1 && snapshot.aliveCount==0,"cleanup affected other session");
            survivor.close();
        }
        std::cout<<"PASS real launcher dies before ranks; other session survives\n";
        check(resources()==baseline,"temporary resources leaked");
        std::cout<<"MPI client: OK\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

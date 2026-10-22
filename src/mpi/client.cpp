#include "mpi/client.hpp"
#include "mpi/counts.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <stdexcept>

extern char** environ;
namespace conway::mpi {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool descendant(int pid,int ancestor) {
    for(int depth=0;depth<1024 && pid>1;++depth) {
        if(pid==ancestor) return true;
        std::ifstream stat("/proc/"+std::to_string(pid)+"/stat");
        std::string line; std::getline(stat,line);
        const auto end=line.rfind(')');
        if(end==std::string::npos) return false;
        std::istringstream fields(line.substr(end+1)); char state; int parent=0;
        if(!(fields>>state>>parent) || parent==pid) return false;
        pid=parent;
    }
    return false;
}
struct SpawnSettings {
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    SpawnSettings() {
        require(posix_spawn_file_actions_init(&actions)==0,"MPI client: spawn actions failed");
        if(posix_spawnattr_init(&attributes)!=0) {
            posix_spawn_file_actions_destroy(&actions);
            throw std::runtime_error("MPI client: spawn attributes failed");
        }
    }
    ~SpawnSettings() { posix_spawnattr_destroy(&attributes); posix_spawn_file_actions_destroy(&actions); }
};
}
Client::Client(ClientOptions options) : options_(std::move(options)) {
    try {
        require(options_.processes>0 && options_.processes<=1024, "MPI client: P must be in 1..1024");
        require(!options_.launcher.empty() && !options_.worker.empty(), "MPI client: missing executable path");
        require(options_.startupTimeout.count()>0 && options_.operationTimeout.count()>0 &&
                options_.closeTimeout.count()>0, "MPI client: invalid timeout");
        const auto deadline=Clock::now()+options_.startupTimeout;
        require(getrandom(&session_,sizeof(session_),0)==sizeof(session_), "MPI client: random session failed");
        char path[]="/tmp/conway-mpi-XXXXXX";
        directory_.reserve(sizeof(path));
        const auto dir=mkdtemp(path);
        require(dir,"MPI client: mkdtemp failed");
        directory_=dir; socket_=directory_+"/control";
        listener_=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        require(listener_>=0,"MPI client: socket failed");
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        require(socket_.size()<sizeof(address.sun_path),"MPI client: socket path too long");
        std::memcpy(address.sun_path,socket_.c_str(),socket_.size()+1);
        require(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0 &&
                listen(listener_,1)==0,"MPI client: listen failed");

        std::vector<std::string> args{options_.launcher};
        args.insert(args.end(),options_.launcherArguments.begin(),options_.launcherArguments.end());
        args.insert(args.end(),{"--host","localhost","-np",std::to_string(options_.processes),options_.worker,
                                "--socket",socket_,"--session",std::to_string(session_)});
        std::vector<char*> argv;
        for (auto& a:args) argv.push_back(a.data());
        argv.push_back(nullptr);
        int pipes[2];
        require(pipe2(pipes,O_CLOEXEC)==0,"MPI client: log pipe failed");
        log_=pipes[0];
        logWrite_=pipes[1];
        require(fcntl(log_,F_SETFL,O_NONBLOCK)==0,"MPI client: log nonblocking failed");
        SpawnSettings spawn;
        require(posix_spawn_file_actions_adddup2(&spawn.actions,logWrite_,STDOUT_FILENO)==0 &&
                posix_spawn_file_actions_adddup2(&spawn.actions,logWrite_,STDERR_FILENO)==0 &&
                posix_spawn_file_actions_addopen(&spawn.actions,STDIN_FILENO,"/dev/null",O_RDONLY,0)==0 &&
                posix_spawnattr_setflags(&spawn.attributes,POSIX_SPAWN_SETPGROUP)==0 &&
                posix_spawnattr_setpgroup(&spawn.attributes,0)==0,"MPI client: spawn setup failed");
        const int error=posix_spawnp(&pid_,argv[0],&spawn.actions,&spawn.attributes,argv.data(),environ);
        ::close(logWrite_); logWrite_=-1;
        if (error) { pid_=-1; throw std::runtime_error(std::string("MPI launch failed: ")+std::strerror(error)); }

        while (fd_<0) {
            tick();
            require(Clock::now()<deadline,"MPI startup: timeout");
            fd_=accept4(listener_,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);
            if (fd_<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)
                throw std::runtime_error("MPI client: accept failed");
            if (fd_<0) { pollfd p{listener_,POLLIN,0}; poll(&p,1,10); }
        }
        ::close(listener_); listener_=-1;
        ucred peer{}; socklen_t length=sizeof(peer);
        require(getsockopt(fd_,SOL_SOCKET,SO_PEERCRED,&peer,&length)==0 && peer.uid==getuid(),
                "MPI client: unexpected peer");
        const auto ready=wire::receive(fd_,deadline,[this]{tick();},[&](const auto& header,auto size){
            require(header.type==wire::Ready && header.session==session_ && header.request==0 &&
                    size==8*(static_cast<std::uint64_t>(options_.processes)+1),"MPI client: invalid handshake");
        });
        std::size_t offset=0;
        require(wire::get(ready.payload,offset)==static_cast<std::uint64_t>(options_.processes),
                "MPI client: process count mismatch");
        std::set<int> seen;
        workers_.reserve(options_.processes);
        workerHandles_.reserve(options_.processes);
        for (int i=0;i<options_.processes;++i) {
            const auto id=wire::get(ready.payload,offset);
            require(id>0 && id<=static_cast<std::uint64_t>(INT32_MAX),"MPI client: invalid worker PID");
            require(seen.insert(id).second && (i!=0 || id==static_cast<std::uint64_t>(peer.pid)),
                    "MPI client: unexpected or duplicate worker PID");
            const int handle=static_cast<int>(syscall(SYS_pidfd_open,static_cast<int>(id),0));
            require(handle>=0,"MPI client: pidfd unavailable (Linux 5.3+ required)");
            try {
                pollfd alive{handle,POLLIN,0};
                require(descendant(id,pid_) && poll(&alive,1,0)==0,
                        "MPI client: worker is not a live descendant of this launcher");
            } catch(...) {
                ::close(handle);
                throw;
            }
            // Only verified handles enter the set that cleanup may signal.
            workerHandles_.push_back(handle);
            workers_.push_back(static_cast<int>(id));
        }
        require(offset==ready.payload.size(),"MPI client: trailing handshake data");
        require(Clock::now()<deadline,"MPI startup: timeout");
    } catch (const std::exception& e) {
        cleanup();
        throw std::runtime_error(std::string(e.what())+(diagnostic_.empty()?"":"\n"+diagnostic_));
    }
}
Client::~Client() { try { close(); } catch(...) {} cleanup(); }
void Client::drain() noexcept {
    try {
    char buffer[2048];
    // Bound both retained diagnostics and work per tick, even for noisy children.
    for (int i=0;i<16 && log_>=0;++i) {
        const auto n=read(log_,buffer,sizeof(buffer));
        if(n<=0) break;
        for (ssize_t j=0;j<n;++j) if(buffer[j]) diagnostic_.push_back(buffer[j]);
        if(diagnostic_.size()>32768) diagnostic_.erase(0,diagnostic_.size()-32768);
    }
    } catch(...) { /* Diagnostics must never prevent noexcept cleanup. */ }
}
void Client::tick(bool allowLauncherExit) {
    drain();
    if(pid_>0) {
        siginfo_t info{};
        if(waitid(P_PID,pid_,&info,WEXITED|WNOHANG|WNOWAIT)==0 && info.si_pid && !allowLauncherExit)
            throw std::runtime_error("MPI launcher exited");
    }
}
void Client::cleanup() noexcept {
    if(fd_>=0) { ::close(fd_); fd_=-1; }
    if(listener_>=0) { ::close(listener_); listener_=-1; }
    if(pid_>0) {
        // Keep the child unreaped until all signals have been sent: its PID
        // (and therefore process-group ID) cannot be reused during cleanup.
        for(auto h:workerHandles_) if(h>=0) syscall(SYS_pidfd_send_signal,h,SIGCONT,nullptr,0);
        siginfo_t ownership{};
        const bool owned=waitid(P_PID,pid_,&ownership,WEXITED|WNOHANG|WNOWAIT)==0;
        if(owned) kill(-pid_,SIGTERM);
        for(auto h:workerHandles_) if(h>=0) syscall(SYS_pidfd_send_signal,h,SIGTERM,nullptr,0);
        for(int i=0;i<20;++i) { drain(); poll(nullptr,0,10); }
        for(auto h:workerHandles_) if(h>=0) syscall(SYS_pidfd_send_signal,h,SIGKILL,nullptr,0);
        // Let the launcher reap workers before forcing its own termination.
        for(int i=0;i<50;++i) {
            siginfo_t info{};
            if(waitid(P_PID,pid_,&info,WEXITED|WNOHANG|WNOWAIT)==0 && info.si_pid) break;
            drain(); poll(nullptr,0,10);
        }
        if(owned) kill(-pid_,SIGKILL);
        while(waitpid(pid_,nullptr,0)<0 && errno==EINTR) {}
        pid_=-1;
    }
    for(auto h:workerHandles_) if(h>=0) ::close(h);
    workerHandles_.clear();
    if(log_>=0) { ::close(log_); log_=-1; }
    if(logWrite_>=0) { ::close(logWrite_); logWrite_=-1; }
    if(!socket_.empty()) { unlink(socket_.c_str()); socket_.clear(); }
    if(!directory_.empty()) { rmdir(directory_.c_str()); directory_.clear(); }
}
wire::Frame Client::exchange(std::uint64_t type, wire::Bytes payload, std::uint64_t expected, wire::Deadline deadline) {
    require(fd_>=0,"MPI client: session closed");
    try {
        const auto progress=[this,type]{tick(type==wire::Close);};
        wire::send(fd_,{type,session_,++request_,std::move(payload)},deadline,progress);
        auto reply=wire::receive(fd_,deadline,progress,[&](const auto& header,auto size){
            require(header.session==session_ && header.request==request_,"MPI client: response ID mismatch");
            require(header.type==expected || header.type==wire::Error,"MPI client: unexpected response type");
            require(header.type==wire::Error || (type==wire::Snapshot ? size>=33 : size==0),
                    "MPI client: invalid response length");
        });
        progress();
        if(reply.type==wire::Error) throw std::runtime_error(std::string(reply.payload.begin(),reply.payload.end()));
        require(reply.type==expected,"MPI client: unexpected response type");
        return reply;
    } catch(const std::exception& e) {
        cleanup(); throw std::runtime_error(std::string(e.what())+(diagnostic_.empty()?"":"\n"+diagnostic_));
    }
}
void Client::load(const InputData& input,std::size_t generation) {
    std::unique_lock lock(mutex_,std::try_to_lock); require(lock.owns_lock(),"MPI client: request already in flight");
    const auto deadline=Clock::now()+options_.operationTimeout;
    const auto count=checked_cell_count(input.width,input.height);
    require(count==input.cells.size() && count<=wire::maxPayload-32 &&
            std::all_of(input.cells.begin(),input.cells.end(),[](auto c){return c<=1;}),"MPI client: invalid board");
    wire::Bytes data;
    wire::put(data,input.width); wire::put(data,input.height); wire::put(data,generation);
    data.insert(data.end(),input.cells.begin(),input.cells.end());
    exchange(wire::Load,std::move(data),wire::Reply,deadline);
}
void Client::advance(std::size_t steps) {
    std::unique_lock lock(mutex_,std::try_to_lock); require(lock.owns_lock(),"MPI client: request already in flight");
    require(steps>0 && steps<=100000,"MPI client: invalid step count");
    const auto deadline=Clock::now()+options_.operationTimeout;
    wire::Bytes data; wire::put(data,steps); exchange(wire::Advance,std::move(data),wire::Reply,deadline);
}
RemoteSnapshot Client::snapshot() {
    std::unique_lock lock(mutex_,std::try_to_lock); require(lock.owns_lock(),"MPI client: request already in flight");
    const auto deadline=Clock::now()+options_.operationTimeout;
    auto reply=exchange(wire::Snapshot,{},wire::Reply,deadline);
    try {
        std::size_t offset=0;
        RemoteSnapshot result{};
        result.input.width=wire::get(reply.payload,offset); result.input.height=wire::get(reply.payload,offset);
        result.generation=wire::get(reply.payload,offset); result.aliveCount=wire::get(reply.payload,offset);
        require(checked_cell_count(result.input.width,result.input.height)==reply.payload.size()-offset,
                "MPI client: invalid snapshot length");
        result.input.cells.assign(reply.payload.begin()+offset,reply.payload.end());
        require(std::all_of(result.input.cells.begin(),result.input.cells.end(),[](auto c){return c<=1;}) &&
                static_cast<std::size_t>(std::count(result.input.cells.begin(),result.input.cells.end(),1))==result.aliveCount,
                "MPI client: invalid snapshot population");
        require(Clock::now()<deadline,"MPI snapshot: timeout");
        return result;
    } catch(...) { cleanup(); throw; }
}
void Client::close() {
    std::unique_lock lock(mutex_,std::try_to_lock); require(lock.owns_lock(),"MPI client: request already in flight");
    if(fd_<0) return;
    const auto deadline=Clock::now()+options_.closeTimeout;
    exchange(wire::Close,{},wire::Closed,deadline);
    for(;;) {
        siginfo_t info{};
        if(waitid(P_PID,pid_,&info,WEXITED|WNOHANG|WNOWAIT)==0 && info.si_pid) {
            if(info.si_code!=CLD_EXITED || info.si_status!=0) {
                cleanup(); throw std::runtime_error("MPI close: launcher failed");
            }
            break;
        }
        if(Clock::now()>=deadline) { cleanup(); throw std::runtime_error("MPI close: launcher timeout"); }
        drain(); poll(nullptr,0,10);
    }
    cleanup();
}
} // namespace conway::mpi

#include "core/engines/mpi.hpp"
#include "mpi/counts.hpp"
#include "mpi/local_state.hpp"
#include "mpi/protocol.hpp"
#include <mpi.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
using namespace conway;
using namespace conway::mpi;
using Clock=std::chrono::steady_clock;
auto deadline() { return Clock::now()+std::chrono::seconds(30); }
std::uint64_t number(const std::string& text) {
    std::uint64_t value{};
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{} || end!=text.data()+text.size()) throw std::runtime_error("invalid numeric argument");
    return value;
}
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int run(int rank,int size,int argc,char** argv) {
    require(argc==5 && std::string(argv[1])=="--socket" && std::string(argv[3])=="--session","invalid worker arguments");
    const auto session=number(argv[4]);
    int fd=-1;
    std::vector<int> pids(size);
    const int pid=getpid();
    MPI_Gather(&pid,1,MPI_INT,pids.data(),1,MPI_INT,0,MPI_COMM_WORLD);
    if(rank==0) {
        fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
        require(fd>=0,"worker socket failed");
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        require(std::strlen(argv[2])<sizeof(address.sun_path),"worker socket path too long");
        std::strcpy(address.sun_path,argv[2]);
        require(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"worker connection failed");
        wire::Bytes ready; wire::put(ready,size);
        for(auto id:pids) wire::put(ready,id);
        wire::send(fd,{wire::Ready,session,0,std::move(ready)},deadline());
    }
    std::optional<core::GameState> state;
    RowPartition part;
    std::size_t width=0,height=0;
    std::vector<int> counts(size),offsets(size);
    std::uint64_t lastRequest=0,closeRequest=0;
    auto operationDeadline=Clock::time_point::max();
    for(;;) {
        // Only rank 0 parses IPC. It validates fully before broadcasting an
        // operation, so invalid requests never strand ranks inside a collective.
        std::array<std::uint64_t,5> control{};
        wire::Frame request;
        if(rank==0) {
            for(;;) {
                try {
                    // Idle sessions may wait indefinitely, but once the first
                    // byte arrives the ENTIRE frame/operation gets one deadline.
                    pollfd input{fd,POLLIN,0};
                    int ready=0;
                    do { ready=poll(&input,1,-1); } while(ready<0 && errno==EINTR);
                    require(ready>0,"worker poll failed");
                    operationDeadline=deadline();
                    request=wire::receive(fd,operationDeadline,{},[&](const auto& header,auto){
                        require(header.session==session && header.request>lastRequest,"invalid request identity");
                        require(header.type==wire::Load || header.type==wire::Advance ||
                                header.type==wire::Snapshot || header.type==wire::Close || header.type==wire::RunBatch,"unknown operation");
                    });
                    control[0]=request.type;
                    std::size_t offset=0;
                    if(request.type==wire::Load) {
                        control[1]=wire::get(request.payload,offset);
                        control[2]=wire::get(request.payload,offset);
                        control[3]=wire::get(request.payload,offset);
                        require(control[1]<=SIZE_MAX && control[2]<=SIZE_MAX && control[3]<=SIZE_MAX,"dimension overflow");
                        const auto count=checked_cell_count(control[1],control[2]);
                        require(count<=wire::maxPayload-32 && request.payload.size()-offset==count &&
                            std::all_of(request.payload.begin()+offset,request.payload.end(),[](auto c){return c<=1;}),"invalid board payload");
                    } else if(request.type==wire::Advance || request.type==wire::RunBatch) {
                        require(state.has_value(),"load a board first");
                        control[4]=wire::get(request.payload,offset);
                        if(request.type==wire::RunBatch)
                            require(wire::get(request.payload,offset)==state->generation,"unexpected batch generation");
                        require(offset==request.payload.size() && control[4]>0 && control[4]<=100000 &&
                            control[4]<=SIZE_MAX-state->generation,"invalid advance payload");
                    } else if(request.type==wire::Snapshot || request.type==wire::Close) {
                        require(request.payload.empty(),"unexpected payload");
                        require(request.type==wire::Close || state.has_value(),"load a board first");
                    } else throw std::runtime_error("unknown operation");
                    lastRequest=request.request;
                    break;
                } catch(const std::exception& error) {
                    // EOF/protocol failure means shutdown; a parsed but invalid
                    // request gets an error, then the client decides whether to close.
                    if(request.request && request.session==session) {
                        const std::string message=error.what();
                        try { wire::send(fd,{wire::Error,session,request.request,{message.begin(),message.end()}},operationDeadline); }
                        catch(...) { control[0]=wire::Close; break; }
                        request={};
                        continue;
                    }
                    control[0]=wire::Close; break;
                }
            }
        }
        MPI_Bcast(control.data(),control.size(),MPI_UINT64_T,0,MPI_COMM_WORLD);
        if(control[0]==wire::Close) {
            closeRequest=request.type==wire::Close ? request.request : 0;
            break;
        }
        wire::Bytes response;
        if(control[0]==wire::Load) {
            width=control[1]; height=control[2];
            part=partition_rows(height,size,rank);
            for(int r=0;r<size;++r) {
                const auto p=partition_rows(height,size,r);
                counts[r]=static_cast<int>(p.rowCount*width);
                offsets[r]=static_cast<int>(p.firstRow*width);
            }
            std::vector<std::uint8_t> rows(part.rowCount*width);
            MPI_Scatterv(rank==0?request.payload.data()+24:nullptr,counts.data(),offsets.data(),MPI_UINT8_T,
                         rows.data(),static_cast<int>(rows.size()),MPI_UINT8_T,0,MPI_COMM_WORLD);
            state=make_local_state_from_rows(width,part,rows);
            state->generation=control[3];
        } else if(control[0]==wire::Advance) {
            for(std::uint64_t i=0;i<control[4];++i) core::step_mpi(*state,part,MPI_COMM_WORLD);
        }
        if(control[0]==wire::RunBatch) {
            MPI_Barrier(MPI_COMM_WORLD); // Preparation excluded.
            const double start=MPI_Wtime();
            for(std::uint64_t i=0;i<control[4];++i) core::step_mpi(*state,part,MPI_COMM_WORLD);
            MPI_Barrier(MPI_COMM_WORLD); // Completion included.
            const double localMs=(MPI_Wtime()-start)*1000.0;
            const double maximumMs=core::maximum_rank_milliseconds(localMs,MPI_COMM_WORLD);
            if(rank==0) {
                char runtime[MPI_MAX_LIBRARY_VERSION_STRING]; int length=0;
                MPI_Get_library_version(runtime,&length);
                while(length>0 && runtime[length-1]=='\0') --length;
                wire::put(response,std::bit_cast<std::uint64_t>(maximumMs));
                wire::put(response,state->generation); wire::put(response,size);
                wire::put(response,length);
                response.insert(response.end(),runtime,runtime+length);
            }
        }
        if(control[0]==wire::Snapshot) {
            auto rows=pack_local_rows(*state);
            std::vector<std::uint8_t> board(rank==0?width*height:0);
            MPI_Gatherv(rows.data(),static_cast<int>(rows.size()),MPI_UINT8_T,
                        board.data(),counts.data(),offsets.data(),MPI_UINT8_T,0,MPI_COMM_WORLD);
            unsigned long long local=state->aliveCount,alive=0;
            MPI_Reduce(&local,&alive,1,MPI_UNSIGNED_LONG_LONG,MPI_SUM,0,MPI_COMM_WORLD);
            if(rank==0) {
                wire::put(response,width); wire::put(response,height);
                wire::put(response,state->generation); wire::put(response,alive);
                response.insert(response.end(),board.begin(),board.end());
            }
        }
        // Acknowledge completion on ALL ranks, including allocations after Scatter.
        MPI_Barrier(MPI_COMM_WORLD);
        if(rank==0) wire::send(fd,{control[0]==wire::RunBatch?wire::BatchResult:wire::Reply,session,request.request,std::move(response)},operationDeadline);
    }
    MPI_Finalize();
    if(rank==0) {
        if(closeRequest) {
            try { wire::send(fd,{wire::Closed,session,closeRequest,{}},operationDeadline); } catch(...) {}
        }
        ::close(fd);
    }
    return 0;
}
}
int main(int argc,char** argv) {
    // Also stop if the local runtime parent disappears during launcher failure.
    const auto parent=getppid();
    prctl(PR_SET_PDEATHSIG,SIGKILL);
    if(getppid()!=parent) return 2;
    int provided=0;
    MPI_Init_thread(&argc,&argv,MPI_THREAD_SINGLE,&provided);
    int rank=0,size=0;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank); MPI_Comm_size(MPI_COMM_WORLD,&size);
    try { return run(rank,size,argc,argv); }
    catch(const std::exception& error) {
        std::cerr<<"MPI worker rank "<<rank<<": "<<error.what()<<'\n';
        MPI_Abort(MPI_COMM_WORLD,2);
        return 2;
    }
}

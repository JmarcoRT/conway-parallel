#include "core/engines/mpi.hpp"
#include "core/engines/openmp.hpp"
#include "mpi/counts.hpp"
#include "mpi/local_state.hpp"
#include "mpi/protocol.hpp"
#include <mpi.h>
#include <omp.h>
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
wire::Bytes run_batch(core::GameState& state,const RowPartition& part,std::uint64_t steps,bool hybrid) {
    std::array<int,2> localThreads{std::numeric_limits<int>::max(),0};
    std::vector<int> threads(hybrid && part.rank==0?2*part.size:0);
    MPI_Barrier(MPI_COMM_WORLD); // Preparation excluded.
    const double start=MPI_Wtime();
    if(hybrid) {
        for(std::uint64_t i=0;i<steps;++i) {
            core::step_mpi_openmp(state,part,MPI_COMM_WORLD);
            const int observed=core::last_openmp_team_size();
            localThreads[0]=std::min(localThreads[0],observed);
            localThreads[1]=std::max(localThreads[1],observed);
        }
    } else {
        for(std::uint64_t i=0;i<steps;++i) core::step_mpi(state,part,MPI_COMM_WORLD);
    }
    MPI_Barrier(MPI_COMM_WORLD); // Completion included.
    const double localMs=(MPI_Wtime()-start)*1000.0;
    const double maximumMs=core::maximum_rank_milliseconds(localMs,MPI_COMM_WORLD);
    if(hybrid)
        MPI_Gather(localThreads.data(),2,MPI_INT,threads.data(),2,MPI_INT,0,MPI_COMM_WORLD);
    wire::Bytes response;
    if(part.rank==0) {
        char runtime[MPI_MAX_LIBRARY_VERSION_STRING]; int length=0;
        MPI_Get_library_version(runtime,&length);
        while(length>0 && runtime[length-1]=='\0') --length;
        wire::put(response,std::bit_cast<std::uint64_t>(maximumMs));
        wire::put(response,state.generation); wire::put(response,part.size);
        wire::put(response,length);
        response.insert(response.end(),runtime,runtime+length);
        for(int count:threads) wire::put(response,count);
    }
    return response;
}
int run(int rank,int size,const char* socketPath,std::uint64_t session,int threads) {
    const bool hybrid=threads>0;
    int fd=-1;
    std::vector<int> pids(size);
    const int pid=getpid();
    MPI_Gather(&pid,1,MPI_INT,pids.data(),1,MPI_INT,0,MPI_COMM_WORLD);
    if(rank==0) {
        fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
        require(fd>=0,"worker socket failed");
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        require(std::strlen(socketPath)<sizeof(address.sun_path),"worker socket path too long");
        std::strcpy(address.sun_path,socketPath);
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
        std::array<std::uint64_t,7> control{};
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
                                header.type==wire::TraceStep || header.type==wire::HybridTraceStep || header.type==wire::Snapshot || header.type==wire::Close || header.type==wire::RunBatch,"unknown operation");
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
                    } else if(request.type==wire::TraceStep || request.type==wire::HybridTraceStep) {
                        require(hybrid==(request.type==wire::HybridTraceStep),"trace mode does not match worker");
                        require(state.has_value(),"load a board first");
                        control[4]=wire::get(request.payload,offset);
                        control[5]=wire::get(request.payload,offset);control[6]=wire::get(request.payload,offset);
                        require(offset==request.payload.size() && control[4]<=SIZE_MAX && control[5]<=SIZE_MAX && control[6]<=SIZE_MAX,
                            "invalid trace request");
                        require(control[4]==state->generation,"unexpected trace generation");
                        (void)trace_payload_size(width,height,size,{static_cast<std::size_t>(control[4]),static_cast<std::size_t>(control[5]),static_cast<std::size_t>(control[6])},threads);
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
                    // No parsed request means ERROR cannot be addressed safely.
                    // Keep the rejection in stderr for the supervising client.
                    std::cerr<<"MPI worker protocol rejection: "<<error.what()<<'\n';
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
            for(std::uint64_t i=0;i<control[4];++i) {
                if(hybrid) core::step_mpi_openmp(*state,part,MPI_COMM_WORLD);
                else core::step_mpi(*state,part,MPI_COMM_WORLD);
            }
        }
        if(control[0]==wire::TraceStep || control[0]==wire::HybridTraceStep) {
            const TraceRequest selection{static_cast<std::size_t>(control[4]),static_cast<std::size_t>(control[5]),static_cast<std::size_t>(control[6])};
            RankStepTrace capture;
            std::vector<int> traceCounts,traceOffsets;
            int ready=1,allReady=0;
            // Allocate the snapshots and gather target BEFORE evolution. Every
            // rank reaches this agreement even if one allocation fails.
            try {
                capture=prepare_rank_trace(width,part,selection,threads);
                traceCounts.resize(size);traceOffsets.resize(size);
                int offset=0;
                for(int r=0;r<size;++r) {
                    traceOffsets[r]=offset;
                    traceCounts[r]=static_cast<int>(rank_trace_payload_size(width,partition_rows(height,size,r),selection,threads));
                    offset+=traceCounts[r];
                }
                if(rank==0) response.resize(trace_payload_size(width,height,size,selection,threads));
            } catch(const std::exception& e) {
                std::cerr<<"MPI trace preparation rank "<<rank<<": "<<e.what()<<'\n';ready=0;
            }
            MPI_Allreduce(&ready,&allReady,1,MPI_INT,MPI_MIN,MPI_COMM_WORLD);
            require(allReady,"MPI trace: capture preparation failed");
            const auto globalAlive=hybrid?core::step_mpi_openmp(*state,part,MPI_COMM_WORLD,&capture):
                core::step_mpi(*state,part,MPI_COMM_WORLD,&capture);
#ifdef CONWAY_MPI_TRACE_FAIL_AFTER_STEP
            // Compiled only into the dedicated failure-injection test worker.
            if(rank==0) throw std::runtime_error("injected trace failure after generation "+std::to_string(state->generation));
#endif
            wire::Bytes record;
            ready=1;
            try {record=encode_rank_trace(capture,width,selection);}
            catch(const std::exception& e) {
                std::cerr<<"MPI trace encoding rank "<<rank<<": "<<e.what()<<'\n';ready=0;
            }
            MPI_Allreduce(&ready,&allReady,1,MPI_INT,MPI_MIN,MPI_COMM_WORLD);
            require(allReady,"MPI trace: capture encoding failed");
            const unsigned long long localBefore=capture.aliveBefore;unsigned long long globalBefore=0;
            MPI_Reduce(&localBefore,&globalBefore,1,MPI_UNSIGNED_LONG_LONG,MPI_SUM,0,MPI_COMM_WORLD);
            MPI_Gatherv(record.data(),static_cast<int>(record.size()),MPI_UINT8_T,
                rank==0?response.data()+72:nullptr,traceCounts.data(),traceOffsets.data(),MPI_UINT8_T,0,MPI_COMM_WORLD);
            if(rank==0) {
                wire::Bytes header;
                for(auto v:{std::uint64_t(width),std::uint64_t(height),control[4],control[4]+1,
                    std::uint64_t(globalBefore),std::uint64_t(globalAlive),control[5],control[6],std::uint64_t(size)}) wire::put(header,v);
                std::copy(header.begin(),header.end(),response.begin());
                (void)decode_step_trace(response,threads); // Audit the assembled real capture before publishing.
            }
        }
        if(control[0]==wire::RunBatch) {
            response=run_batch(*state,part,control[4],hybrid);
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
        if(rank==0) {
            auto type=wire::Reply;
            if(control[0]==wire::TraceStep) type=wire::TraceResult;
            else if(control[0]==wire::HybridTraceStep) type=wire::HybridTraceResult;
            else if(control[0]==wire::RunBatch) type=wire::BatchResult;
            wire::send(fd,{type,session,request.request,std::move(response)},operationDeadline);
        }
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
    int threads=0;
    std::uint64_t session=0;
    try {
        require((argc==5 || argc==7) && std::string(argv[1])=="--socket" &&
                std::string(argv[3])=="--session","invalid worker arguments");
        session=number(argv[4]);
        if(argc==7) {
            require(std::string(argv[5])=="--threads","invalid worker arguments");
            const auto count=number(argv[6]);
            require(count>0 && count<=std::numeric_limits<int>::max(),"invalid OpenMP thread count");
            threads=static_cast<int>(count);
        }
    } catch(const std::exception& error) {
        std::cerr<<"MPI worker: "<<error.what()<<'\n';
        return 2;
    }
    const int required=threads>0?MPI_THREAD_FUNNELED:MPI_THREAD_SINGLE;
    int provided=0;
    if(MPI_Init_thread(&argc,&argv,required,&provided)!=MPI_SUCCESS) return 2;
    int rank=0,size=0;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank); MPI_Comm_size(MPI_COMM_WORLD,&size);
    try {
        require(provided>=required,"MPI runtime does not provide the required thread level");
        if(threads>0) {
            omp_set_dynamic(0);
            omp_set_num_threads(threads);
        }
        return run(rank,size,argv[2],session,threads);
    }
    catch(const std::exception& error) {
        std::cerr<<"MPI worker rank "<<rank<<": "<<error.what()<<'\n';
        MPI_Abort(MPI_COMM_WORLD,2);
        return 2;
    }
}

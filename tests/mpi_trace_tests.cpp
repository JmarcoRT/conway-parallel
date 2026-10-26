#include "mpi/client.hpp"
#include "mpi/local_state.hpp"
#include "core/engines/mpi.hpp"
#include "core/engines/sequential.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
using namespace conway;
using namespace conway::mpi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
template<class F> void rejects(F&& f,const char* message) {
    bool failed=false;try {f();} catch(const std::exception&) {failed=true;}check(failed,message);
}
void gone(const Client& client,const std::vector<int>& pids,int launcher,const std::string& dir) {
    check(client.cleanupConfirmed(),"cleanup not confirmed");
    check(!std::filesystem::exists(dir),"socket directory survived");
    check(kill(launcher,0)<0,"launcher survived");
    for(auto pid:pids) check(kill(pid,0)<0,"rank survived");
}
InputData input(std::size_t height) {
    InputData in{5,height,std::vector<std::uint8_t>(height*5)};
    if(height==6) for(auto row:{1u,2u,3u}) in.cells[row*5+2]=1;
    else for(std::size_t i=0;i<in.cells.size();++i) in.cells[i]=((i*17+11)%13)<5;
    return in;
}
std::vector<std::uint8_t> extractRow(const core::GameState& full,std::size_t row) {
    const auto first=full.current.begin()+(row+1)*full.stride+1;return {first,first+full.width};
}
void inspect(const RankStepTrace& t,const core::GameState& before,const core::GameState& after,const TraceRequest& q) {
    const auto& p=t.partition;const auto w=before.width,stride=w+2;
    validate_rank_trace(t,w,q);
    for(std::size_t row=0;row<p.rowCount;++row) for(std::size_t col=0;col<w;++col) {
        check(t.currentBefore[(row+1)*stride+col+1]==before.current[(p.firstRow+row+1)*stride+col+1],"captured input differs from oracle input");
        check(t.currentAfter[(row+1)*stride+col+1]==after.current[(p.firstRow+row+1)*stride+col+1],"captured output differs from full oracle");
    }
    for(const auto& e:t.exchanges) {
        if(e.sentGlobalRow) check(e.sent==extractRow(before,*e.sentGlobalRow),"sent halo differs from original row");
        if(e.receivedGlobalRow) check(e.received==extractRow(before,*e.receivedGlobalRow),"received halo differs from original remote row");
        if(e.sendTo==noPeer) check(e.sent.empty(),"invented PROC_NULL send");
        if(e.receivedFrom==noPeer) check(e.received.empty(),"invented PROC_NULL receive");
    }
    if(p.empty()) check(t.exchanges.empty() && !t.cell && t.aliveBefore==0 && t.aliveAfter==0,"empty rank recorded computation/messages");
    if(before.height==6 && p.size==2 && p.rank==0 && q.expectedGeneration==0) {
        check(t.cell && t.cell->row==3 && t.cell->column==2,"boundary selection wrong");
        const auto& c=t.cell->evaluation;
        check(c.before==0 && c.neighborCount==3 && c.after==1 && c.neighbors==std::array<std::uint8_t,8>{0,0,1,0,1,0,0,1},"boundary cell not computed using received live neighbor");
        check(t.exchanges[0].receivedFrom==1 && t.exchanges[0].receivedGlobalRow==3 && t.exchanges[0].receiveLocalRow==4,"lower halo origin/destination wrong");
        check(t.exchanges[1].sendTo==1 && t.exchanges[1].sentGlobalRow==2,"upper halo send row wrong");
    }
}
int engine(int argc,char** argv) {
    MPI_Init(&argc,&argv);int rank=0,p=0;MPI_Comm_rank(MPI_COMM_WORLD,&rank);MPI_Comm_size(MPI_COMM_WORLD,&p);
    try {
        for(std::size_t h:{1u,5u,6u}) {
            auto in=input(h);const auto part=partition_rows(h,p,rank);
            auto local=make_local_state(in,part),plain=local;
            auto reference=make_local_state(in,partition_rows(h,1,0));
            for(std::size_t g=0;g<6;++g) {
                auto before=reference;core::step_sequential(reference);
                const TraceRequest selection{g,h==6?2u:0u,1};
                auto capture=prepare_rank_trace(in.width,part,selection);
                const auto rawBefore=local.current;const auto* current=local.current.data();const auto* next=local.next.data();
                const auto alive=core::step_mpi(local,part,MPI_COMM_WORLD,&capture);
                const auto plainAlive=core::step_mpi(plain,part,MPI_COMM_WORLD);
                check(alive==reference.aliveCount && alive==plainAlive,"global population differs");
                check(local.current==plain.current && local.next==plain.next && local.aliveCount==plain.aliveCount && local.generation==g+1,"capture changed real evolution");
                check(capture.currentBefore==rawBefore && capture.currentWithHalos==local.next && capture.currentAfter==local.current &&
                    local.current.data()==next && local.next.data()==current && capture.buffersSwapped,"double buffer capture wrong");
                inspect(capture,before,reference,selection);
                const auto bytes=encode_rank_trace(capture,in.width,selection);
                check(encode_rank_trace(decode_rank_trace(bytes,in.width,part,selection),in.width,selection)==bytes,"rank codec roundtrip");
            }
            // Reject malformed preparation on ONE rank collectively, before
            // clearing halos, exchanging rows, evaluating cells or swapping.
            for(int invalidKind=0;invalidKind<7;++invalidKind) {
                auto invalid=prepare_rank_trace(in.width,part,{6,0,0});
                const int target=invalidKind==0?p-1:0;
                if(rank==target) {
                    switch(invalidKind) {
                        case 0: invalid.currentAfter.clear();break;
                        case 1: invalid.exchanges[0].tag=17;break;
                        case 2: invalid.exchanges[0].sendTo=rank;break;
                        case 3: invalid.exchanges[0].receivedGlobalRow=0;break;
                        case 4: invalid.exchanges[0].receiveLocalRow=0;break;
                        case 5: invalid.cell.reset();break;
                        case 6: invalid.selection.row=h;break;
                    }
                }
                const auto untouched=local;
                rejects([&]{core::step_mpi(local,part,MPI_COMM_WORLD,&invalid);},"incompatible capture was not rejected collectively");
                check(local.current==untouched.current && local.next==untouched.next && local.generation==untouched.generation && local.aliveCount==untouched.aliveCount,"rejected capture mutated state");
            }
        }
        if(rank==0) std::cout<<"PASS engine capture P="<<p<<": halos, boundary cell, actual buffers, empty ranks, 6 generations, untraced equivalence\n";
        MPI_Finalize();return 0;
    } catch(const std::exception& e) {std::cerr<<"rank "<<rank<<": "<<e.what()<<'\n';MPI_Abort(MPI_COMM_WORLD,2);return 2;}
}
MpiStepTrace fixture(std::size_t generation=0) {
    MpiStepTrace t;t.width=t.height=1;t.generationBefore=generation;t.generationAfter=generation+1;
    t.selection={generation,0,0};t.aliveBefore=1;t.aliveAfter=0;
    auto r=prepare_rank_trace(1,partition_rows(1,1,0),t.selection);r.generationAfter=generation+1;
    r.currentBefore[4]=r.currentWithHalos[4]=1;r.aliveBefore=1;r.buffersSwapped=true;
    r.cell->captured=true;r.cell->evaluation.before=1;t.ranks.push_back(std::move(r));return t;
}
int fake(int argc,char** argv) {
    const std::string mode=argv[1];std::string path;std::uint64_t session=0;
    for(int i=2;i+1<argc;++i) {
        if(std::string(argv[i])=="--socket") path=argv[i+1];
        if(std::string(argv[i])=="--session") session=std::stoull(argv[i+1]);
    }
    const int fd=socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un address{};address.sun_family=AF_UNIX;
    std::copy(path.begin(),path.end(),address.sun_path);
    check(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"fake connection");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    wire::Bytes hello;wire::put(hello,1);wire::put(hello,getpid());
    if(mode=="--fake-ready-version") {
        wire::Bytes header;
        for(auto n:{std::uint64_t{0x434f4e5741594d50},std::uint64_t{2},std::uint64_t{wire::Ready},session,std::uint64_t{0},std::uint64_t(hello.size())}) wire::put(header,n);
        ::send(fd,header.data(),header.size(),MSG_NOSIGNAL);
        for(;;) pause();
    }
    wire::send(fd,{wire::Ready,session,0,hello},deadline);
    const auto load=wire::receive(fd,deadline);wire::send(fd,{wire::Reply,session,load.request,{}},deadline);
    const auto request=wire::receive(fd,deadline);check(request.type==wire::TraceStep,"fake expected TRACE_STEP");
    auto bytes=encode_step_trace(fixture(mode=="--fake-generation"?1:0));
    if(mode=="--fake-binary") bytes[72+64+4]=2;
    if(mode=="--fake-version" || mode=="--fake-length" || mode=="--fake-prefix") {
        wire::Bytes header;
        const auto length=mode=="--fake-length"?maxTraceBytes+1:bytes.size();
        for(auto n:{std::uint64_t{0x434f4e5741594d50},mode=="--fake-version"?std::uint64_t{2}:wire::version,
            std::uint64_t{wire::TraceResult},session,request.request,std::uint64_t(length)}) wire::put(header,n);
        ::send(fd,header.data(),header.size(),MSG_NOSIGNAL);
        if(mode=="--fake-prefix") {
            // Claim an otherwise valid bounded header for 2 columns but the
            // 1-column payload length: must reject before reading bulk bytes.
            bytes[7]=2;::send(fd,bytes.data(),72,MSG_NOSIGNAL);
        }
    } else wire::send(fd,{wire::TraceResult,session,request.request,bytes},deadline);
    for(;;) pause();
}
void codecs() {
    auto t=fixture();const auto bytes=encode_step_trace(t);
    check(encode_step_trace(decode_step_trace(bytes))==bytes,"global codec roundtrip");
    for(bool output:{false,true}) {
        auto bad=t;(output?bad.ranks[0].currentAfter:bad.ranks[0].currentBefore)[1]=1;
        rejects([&]{encode_step_trace(bad);},"nonzero global border accepted in stale/result buffer");
    }
    {
        auto empty=prepare_rank_trace(1,partition_rows(1,2,1),{0,0,0});
        empty.generationAfter=1;empty.buffersSwapped=true;
        validate_rank_trace(empty,1,{0,0,0});
        for(bool output:{false,true}) {
            auto bad=empty;(output?bad.currentAfter:bad.currentBefore)[4]=1;
            rejects([&]{validate_rank_trace(bad,1,{0,0,0});},"nonzero empty-rank halo accepted");
        }
    }
    auto changed=bytes;changed.push_back(0);rejects([&]{decode_step_trace(changed);},"trailing trace accepted");
    changed=bytes;changed.pop_back();rejects([&]{decode_step_trace(changed);},"truncated trace accepted");
    for(auto offset:{std::size_t(72+64+4),std::size_t(72+7*8+7),bytes.size()-1}) {
        changed=bytes;changed[offset]=2;rejects([&]{decode_step_trace(changed);},"corrupt capture accepted");
    }
    rejects([&]{trace_payload_size(16385,1,1,{0,0,0});},"trace cell limit missing");
    rejects([&]{trace_payload_size(1,1,129,{0,0,0});},"trace process limit missing");
    rejects([&]{trace_payload_size(16384,1,128,{0,0,0});},"aggregate halo overhead limit missing");
    rejects([&]{trace_payload_size(1,1,1,{SIZE_MAX,0,0});},"generation overflow accepted");
    rejects([&]{trace_payload_size(1,1,1,{0,1,0});},"bad cell accepted");
    std::cout<<"PASS capture codec corruption, size/P/cell/generation limits\n";
}
int clientTests(int argc,char** argv) {
    check(argc==4,"expected launcher, worker and post-step failure worker");codecs();
    ClientOptions options;options.launcher=argv[1];options.worker=argv[2];options.launcherArguments={"--oversubscribe"};
    options.startupTimeout=std::chrono::seconds(5);options.operationTimeout=std::chrono::seconds(2);
    for(int p:{1,2,4,7}) {
        options.processes=p;Client client(options);const auto pids=client.workerPids();const auto launcher=client.launcherPid();const auto dir=client.resourceDirectory();
        for(std::size_t h:{1u,5u,6u}) {
            const auto in=input(h);client.load(in);auto reference=make_local_state(in,partition_rows(h,1,0));
            rejects([&]{client.traceStep({0,h,0});},"client accepted bad selection");
            check(client.snapshot().generation==0,"invalid local request mutated state");
            for(std::size_t g=0;g<6;++g) {
                const auto before=reference;core::step_sequential(reference);
                const TraceRequest q{g,h==6?2u:0u,1};const auto trace=client.traceStep(q);
                check(trace.ranks.size()==static_cast<std::size_t>(p) && trace.aliveBefore==before.aliveCount && trace.aliveAfter==reference.aliveCount && trace.generationAfter==g+1,"aggregate trace metadata mismatch");
                for(const auto& r:trace.ranks) inspect(r,before,reference,q);
                const auto snapshot=client.snapshot();check(snapshot.input.cells==pack_local_rows(reference) && snapshot.generation==g+1 && snapshot.aliveCount==trace.aliveAfter,"TRACE_STEP did not commit real state");
                if(g==0) {
                    auto corrupt=trace;corrupt.ranks[0].partition.firstRow++;
                    rejects([&]{validate_step_trace(corrupt);},"invalid rank identity accepted");
                }
            }
            // The ordinary commands still work on the very same buffers.
            client.advance();core::step_sequential(reference);
            const auto metrics=client.runBatch(2,7);core::step_sequential(reference);core::step_sequential(reference);
            check(metrics.generation==9 && client.snapshot().input.cells==pack_local_rows(reference),"ADVANCE/RUN_BATCH interoperation failed");
        }
        client.close();gone(client,pids,launcher,dir);
    }
    std::cout<<"PASS TRACE_STEP over IPC P=1,2,4,7; uneven/empty partitions; multiple generations; ADVANCE/RUN_BATCH\n";
    // Exceed the DIDACTIC cell limit, but exercise the ordinary client/worker
    // commands successfully on the exact same board after the local rejection.
    options.processes=2;
    {
        const InputData large{129,129,std::vector<std::uint8_t>(129*129,1)};
        Client client(options);const auto pids=client.workerPids();const auto launcher=client.launcherPid();const auto dir=client.resourceDirectory();
        client.load(large);
        rejects([&]{client.traceStep({0,0,0});},"didactic cell limit bypassed");
        const auto before=client.snapshot();check(before.generation==0 && before.input.cells==large.cells && before.aliveCount==large.cells.size(),"rejected capture changed state");
        client.advance();client.runBatch(2,1);
        auto reference=make_local_state(large,partition_rows(large.height,1,0));
        for(int i=0;i<3;++i) core::step_sequential(reference);
        const auto after=client.snapshot();check(after.generation==3 && after.input.cells==pack_local_rows(reference) && after.aliveCount==reference.aliveCount,"didactic limits leaked into ADVANCE/RUN_BATCH");
        client.close();gone(client,pids,launcher,dir);
    }
    {
        auto failed=options;failed.worker=argv[3];Client client(failed);
        const auto pids=client.workerPids();const auto launcher=client.launcherPid();const auto dir=client.resourceDirectory();
        client.load(input(6));bool cleaned=false;std::string diagnostic;
        try {client.traceStep({0,2,1});} catch(const ClientError& e) {cleaned=e.cleanupConfirmed;diagnostic=e.what();}
        const std::string marker="injected trace failure after generation 1";
        check(cleaned && diagnostic.find(marker)!=std::string::npos,"post-advance failure lost diagnosis/cleanup");
        check(diagnostic.find(marker,diagnostic.find(marker)+marker.size())==std::string::npos && diagnostic.find("after generation 2")==std::string::npos,"failed step was retried");
        gone(client,pids,launcher,dir);
        rejects([&]{client.traceStep({0,2,1});},"closed job accepted a retry");
        check(client.launcherPid()<0,"retry restarted a job implicitly");
    }
    std::cout<<"PASS limits isolated from ordinary evolution; real post-advance failure is not retried and cleans job\n";
    options.processes=1;
    for(const std::string mode:{"--fake-binary","--fake-generation","--fake-version","--fake-length","--fake-prefix"}) {
        auto fakeOptions=options;fakeOptions.launcher=std::filesystem::canonical(argv[0]);fakeOptions.launcherArguments={mode};
        Client client(fakeOptions);const auto pids=client.workerPids();const auto launcher=client.launcherPid();const auto dir=client.resourceDirectory();
        client.load({1,1,{1}});bool cleaned=false;std::string diagnostic;
        try {client.traceStep({0,0,0});} catch(const ClientError& e) {cleaned=e.cleanupConfirmed;diagnostic=e.what();}
        const auto expected=mode=="--fake-binary"?"nonbinary":mode=="--fake-generation"?"identity mismatch":
            mode=="--fake-version"?"incompatible version (expected 3, received 2)":mode=="--fake-length"?"invalid message length":"payload length mismatch";
        check(cleaned && diagnostic.find(expected)!=std::string::npos,"invalid TRACE_RESULT did not preserve expected diagnosis/confirmed cleanup");gone(client,pids,launcher,dir);
    }
    {
        auto old=options;old.launcher=std::filesystem::canonical(argv[0]);old.launcherArguments={"--fake-ready-version"};
        bool rejected=false;
        try {Client client(old);} catch(const ClientError& e) {
            rejected=e.cleanupConfirmed && std::string(e.what()).find("incompatible version (expected 3, received 2)")!=std::string::npos;
        }
        check(rejected,"old worker handshake did not retain version diagnosis/cleanup");
    }
    std::cout<<"PASS malformed/stale TRACE_RESULT, incompatible version and early length/prefix rejection with cleanup\n";
    options.processes=2;
    for(int signal:{SIGSTOP,SIGKILL}) {
        Client client(options);client.load(input(6));const auto pids=client.workerPids();const auto launcher=client.launcherPid();const auto dir=client.resourceDirectory();
        check(kill(pids.back(),signal)==0,"could not inject rank failure");bool cleaned=false;
        try {client.traceStep({0,2,1});} catch(const ClientError& e) {cleaned=e.cleanupConfirmed && !std::string(e.what()).empty();}
        check(cleaned,"trace rank failure did not retain diagnostic/cleanup");gone(client,pids,launcher,dir);
    }
    {
        Client client(options);client.load(input(6));const auto pids=client.workerPids();const auto launcher=client.launcherPid();const auto dir=client.resourceDirectory();
        bool cleaned=false;try {client.traceStep({99,2,1});} catch(const ClientError& e) {cleaned=e.cleanupConfirmed;}
        check(cleaned,"unexpected generation did not clean session");gone(client,pids,launcher,dir);
    }
    std::cout<<"PASS trace timeout/disconnect/stale request diagnostics and confirmed cleanup\n";return 0;
}
}
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])=="--engine") return engine(argc,argv);
    try {
        if(argc>1 && std::string(argv[1]).starts_with("--fake-")) return fake(argc,argv);
        return clientTests(argc,argv);
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#include "mpi/step_trace.hpp"
#include "mpi/counts.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace conway::mpi {
namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool owns(const RowPartition& p,const TraceRequest& q) {return q.row>=p.firstRow && q.row<p.endRow();}
int above(const RowPartition& p) {return p.empty() || !p.firstRow?noPeer:p.rank-1;}
int below(const RowPartition& p) {return p.empty() || p.endRow()==p.globalHeight?noPeer:p.rank+1;}
HaloExchange exchangeMetadata(const RowPartition& p,int tag) {
    HaloExchange e;e.tag=tag;e.sendTo=tag==0?above(p):below(p);e.receivedFrom=tag==0?below(p):above(p);
    e.receiveLocalRow=tag==0?p.rowCount+1:0;
    if(e.sendTo!=noPeer) e.sentGlobalRow=tag==0?p.firstRow:p.endRow()-1;
    if(e.receivedFrom!=noPeer) e.receivedGlobalRow=tag==0?p.endRow():p.firstRow-1;
    return e;
}
std::size_t padded(std::size_t width,const RowPartition& p) {return (width+2)*(p.rowCount+2);}
std::size_t rankSize(std::size_t width,const RowPartition& p,const TraceRequest& q) {
    return 64+3*padded(width,p)+(p.empty()?0:96+2*width*((above(p)!=noPeer)+(below(p)!=noPeer)))+(owns(p,q)?35:0);
}
std::vector<std::uint8_t> row(const std::vector<std::uint8_t>& b,std::size_t w,std::size_t r) {
    const auto first=b.begin()+r*(w+2)+1;return {first,first+w};
}
std::size_t population(const std::vector<std::uint8_t>& b,std::size_t w,const RowPartition& p) {
    std::size_t total=0;
    for(std::size_t r=1;r<=p.rowCount;++r) for(std::size_t c=1;c<=w;++c) total+=b[r*(w+2)+c];
    return total;
}
void binary(const std::vector<std::uint8_t>& b) {
    require(std::all_of(b.begin(),b.end(),[](auto v){return v<=1;}),"MPI trace: nonbinary cell");
}
struct Reader {
    const wire::Bytes& bytes;std::size_t offset=0;
    std::size_t get() {
        const auto value=wire::get(bytes,offset);
        require(value<=SIZE_MAX,"MPI trace: integer overflow");return static_cast<std::size_t>(value);
    }
    void copy(std::vector<std::uint8_t>& out) {
        require(offset<=bytes.size() && out.size()<=bytes.size()-offset,"MPI trace: truncated cells");
        std::copy_n(bytes.begin()+offset,out.size(),out.begin());offset+=out.size();
    }
    std::uint8_t byte() {require(offset<bytes.size(),"MPI trace: truncated evaluation");return bytes[offset++];}
};
struct Header {MpiStepTrace trace;int processes;};
Header readHeader(Reader& in) {
    Header h;
    auto& t=h.trace;
    t.width=in.get();t.height=in.get();t.generationBefore=in.get();t.generationAfter=in.get();
    t.aliveBefore=in.get();t.aliveAfter=in.get();t.selection={t.generationBefore,in.get(),in.get()};
    const auto p=in.get();require(p>0 && p<=maxTraceProcesses,"MPI trace: process limit");h.processes=static_cast<int>(p);
    (void)trace_payload_size(t.width,t.height,h.processes,t.selection);
    require(t.generationAfter==t.generationBefore+1 && t.aliveBefore<=t.width*t.height && t.aliveAfter<=t.width*t.height,
        "MPI trace: inconsistent header");
    return h;
}
}
std::size_t trace_payload_size(std::size_t w,std::size_t h,int p,const TraceRequest& q) {
    require(checked_cell_count(w,h)<=maxTraceCells,"MPI trace: cell limit (16384)");
    require(p>0 && p<=maxTraceProcesses,"MPI trace: process limit (128)");
    require(q.row<h && q.column<w && q.expectedGeneration<std::numeric_limits<std::size_t>::max(),"MPI trace: invalid selection or generation");
    std::size_t size=72;
    for(int r=0;r<p;++r) size+=rankSize(w,partition_rows(h,p,r),q);
    require(size<=maxTraceBytes,"MPI trace: payload limit (8 MiB)");return size;
}
std::size_t rank_trace_payload_size(std::size_t w,const RowPartition& p,const TraceRequest& q) {
    (void)trace_payload_size(w,p.globalHeight,p.size,q);
    const auto expected=partition_rows(p.globalHeight,p.size,p.rank);
    require(p.firstRow==expected.firstRow && p.rowCount==expected.rowCount,"MPI trace: invalid partition");
    return rankSize(w,p,q);
}
RankStepTrace prepare_rank_trace(std::size_t w,const RowPartition& part,const TraceRequest& q) {
    (void)trace_payload_size(w,part.globalHeight,part.size,q);
    const auto expected=partition_rows(part.globalHeight,part.size,part.rank);
    require(part.firstRow==expected.firstRow && part.rowCount==expected.rowCount,"MPI trace: invalid partition");
    RankStepTrace t;t.partition=part;t.selection=q;t.generationBefore=q.expectedGeneration;
    t.currentBefore.resize(padded(w,part));t.currentWithHalos.resize(t.currentBefore.size());t.currentAfter.resize(t.currentBefore.size());
    if(!part.empty()) {
        t.exchanges.resize(2);
        for(int tag=0;tag<2;++tag) {
            auto& e=t.exchanges[tag];e=exchangeMetadata(part,tag);
            if(e.sendTo!=noPeer) e.sent.resize(w);
            if(e.receivedFrom!=noPeer) e.received.resize(w);
        }
    }
    if(owns(part,q)) t.cell=core::CellProbe{q.row-part.firstRow+1,q.column+1,false,{}};
    return t;
}
void validate_prepared_rank_trace(const RankStepTrace& t,std::size_t w) {
    const auto& p=t.partition;const auto& q=t.selection;
    (void)rank_trace_payload_size(w,p,q); // Checks policy and partition before padded arithmetic.
    require(t.generationBefore==q.expectedGeneration,"MPI trace: prepared generation mismatch");
    for(const auto* b:{&t.currentBefore,&t.currentWithHalos,&t.currentAfter})
        require(b->size()==padded(w,p),"MPI trace: buffer size mismatch");
    require(t.exchanges.size()==(p.empty()?0u:2u),"MPI trace: exchange count mismatch");
    for(std::size_t i=0;i<t.exchanges.size();++i) {
        const auto& e=t.exchanges[i];const auto x=exchangeMetadata(p,static_cast<int>(i));
        require(e.tag==x.tag && e.sendTo==x.sendTo && e.receivedFrom==x.receivedFrom && e.sentGlobalRow==x.sentGlobalRow &&
            e.receivedGlobalRow==x.receivedGlobalRow && e.receiveLocalRow==x.receiveLocalRow &&
            e.sent.size()==(x.sendTo==noPeer?0:w) && e.received.size()==(x.receivedFrom==noPeer?0:w),"MPI trace: exchange metadata mismatch");
    }
    require(t.cell.has_value()==owns(p,q),"MPI trace: cell owner mismatch");
    if(t.cell) require(t.cell->row==q.row-p.firstRow+1 && t.cell->column==q.column+1,"MPI trace: selected cell coordinates mismatch");
}
void validate_rank_trace(const RankStepTrace& t,std::size_t w,const TraceRequest& q) {
    validate_prepared_rank_trace(t,w);const auto& p=t.partition;
    require(t.selection.expectedGeneration==q.expectedGeneration && t.selection.row==q.row && t.selection.column==q.column,
        "MPI trace: selection mismatch");
    require(t.generationAfter==q.expectedGeneration+1 && t.buffersSwapped,"MPI trace: generation or buffer swap mismatch");
    for(const auto* b:{&t.currentBefore,&t.currentWithHalos,&t.currentAfter}) {
        binary(*b);
        for(std::size_t r=0;r<p.rowCount+2;++r)
            require((*b)[r*(w+2)]==0 && (*b)[r*(w+2)+w+1]==0,"MPI trace: nonzero side boundary");
        // Global boundaries are always zero, including stale buffers and empty
        // ranks. Only INTERNAL halos may contain older generations.
        if(above(p)==noPeer) require(std::all_of(b->begin(),b->begin()+w+2,[](auto v){return v==0;}),"MPI trace: upper boundary mismatch");
        if(below(p)==noPeer) require(std::all_of(b->end()-w-2,b->end(),[](auto v){return v==0;}),"MPI trace: lower boundary mismatch");
    }
    require(population(t.currentBefore,w,p)==t.aliveBefore && population(t.currentAfter,w,p)==t.aliveAfter,"MPI trace: local population mismatch");
    for(std::size_t r=1;r<=p.rowCount;++r) require(row(t.currentBefore,w,r)==row(t.currentWithHalos,w,r),"MPI trace: halo exchange modified own rows");
    for(std::size_t i=0;i<t.exchanges.size();++i) {
        const auto& e=t.exchanges[i];
        require((e.sendTo==noPeer || e.sent==row(t.currentBefore,w,i==0?1:p.rowCount)) &&
            (e.receivedFrom==noPeer || e.received==row(t.currentWithHalos,w,e.receiveLocalRow)),"MPI trace: exchange cells mismatch");
    }
    if(t.cell) {
        const auto& c=*t.cell;const auto& v=c.evaluation;
        require(c.captured,"MPI trace: selected cell not captured");
        const auto stride=w+2,index=c.row*stride+c.column;
        const std::array<std::size_t,8> indices{index-stride-1,index-stride,index-stride+1,index-1,index+1,index+stride-1,index+stride,index+stride+1};
        for(std::size_t i=0;i<8;++i) require(v.neighbors[i]==t.currentWithHalos[indices[i]],"MPI trace: neighbor mismatch");
        require(v.before==t.currentWithHalos[index] && v.after==t.currentAfter[index] &&
            v.neighborCount==std::accumulate(v.neighbors.begin(),v.neighbors.end(),0),"MPI trace: evaluation mismatch");
    }
}
void validate_step_trace(const MpiStepTrace& t) {
    require(t.ranks.size()>0 && t.ranks.size()<=maxTraceProcesses,"MPI trace: rank count mismatch");
    (void)trace_payload_size(t.width,t.height,static_cast<int>(t.ranks.size()),t.selection);
    require(t.selection.expectedGeneration==t.generationBefore && t.generationAfter==t.generationBefore+1,"MPI trace: global generation mismatch");
    std::size_t before=0,after=0;
    for(std::size_t r=0;r<t.ranks.size();++r) {
        const auto& rank=t.ranks[r];const auto& p=rank.partition;
        require(p.rank==static_cast<int>(r) && p.size==static_cast<int>(t.ranks.size()) && p.globalHeight==t.height,"MPI trace: rank identity mismatch");
        validate_rank_trace(rank,t.width,t.selection);before+=rank.aliveBefore;after+=rank.aliveAfter;
    }
    for(const auto& rank:t.ranks) {
        for(const auto& e:rank.exchanges) if(e.receivedFrom!=noPeer) {
            const auto& sender=t.ranks[e.receivedFrom];
            require(e.received==row(sender.currentBefore,t.width,*e.receivedGlobalRow-sender.partition.firstRow+1),"MPI trace: received row differs from sender");
        }
    }
    require(before==t.aliveBefore && after==t.aliveAfter,"MPI trace: global population mismatch");
}
wire::Bytes encode_rank_trace(const RankStepTrace& t,std::size_t w,const TraceRequest& q) {
    validate_rank_trace(t,w,q);
    wire::Bytes out;out.reserve(rankSize(w,t.partition,q));
    for(auto v:{std::size_t(t.partition.rank),t.partition.firstRow,t.partition.rowCount,t.generationBefore,t.generationAfter,t.aliveBefore,t.aliveAfter,std::size_t(t.buffersSwapped)}) wire::put(out,v);
    for(const auto* b:{&t.currentBefore,&t.currentWithHalos,&t.currentAfter}) out.insert(out.end(),b->begin(),b->end());
    for(const auto& e:t.exchanges) {
        for(auto v:{std::size_t(e.tag),std::size_t(e.sendTo+1),std::size_t(e.receivedFrom+1),e.sentGlobalRow?*e.sentGlobalRow+1:0,e.receivedGlobalRow?*e.receivedGlobalRow+1:0,e.receiveLocalRow}) wire::put(out,v);
        out.insert(out.end(),e.sent.begin(),e.sent.end());out.insert(out.end(),e.received.begin(),e.received.end());
    }
    if(t.cell) {
        const auto& c=*t.cell;wire::put(out,c.row);wire::put(out,c.column);wire::put(out,c.captured);
        out.push_back(c.evaluation.before);out.insert(out.end(),c.evaluation.neighbors.begin(),c.evaluation.neighbors.end());out.push_back(c.evaluation.neighborCount);out.push_back(c.evaluation.after);
    }
    require(out.size()==rankSize(w,t.partition,q),"MPI trace: internal encoding size mismatch");return out;
}
RankStepTrace decode_rank_trace(const wire::Bytes& bytes,std::size_t w,const RowPartition& p,const TraceRequest& q) {
    require(bytes.size()==rank_trace_payload_size(w,p,q),"MPI trace: rank payload length mismatch");
    auto t=prepare_rank_trace(w,p,q);Reader in{bytes};
    require(in.get()==static_cast<std::size_t>(p.rank) && in.get()==p.firstRow && in.get()==p.rowCount,"MPI trace: encoded partition mismatch");
    t.generationBefore=in.get();t.generationAfter=in.get();t.aliveBefore=in.get();t.aliveAfter=in.get();const auto swapped=in.get();
    require(swapped==1,"MPI trace: swap flag mismatch");t.buffersSwapped=true;
    in.copy(t.currentBefore);in.copy(t.currentWithHalos);in.copy(t.currentAfter);
    for(auto& e:t.exchanges) {
        require(in.get()==static_cast<std::size_t>(e.tag) && in.get()==static_cast<std::size_t>(e.sendTo+1) && in.get()==static_cast<std::size_t>(e.receivedFrom+1) &&
            in.get()==(e.sentGlobalRow?*e.sentGlobalRow+1:0) && in.get()==(e.receivedGlobalRow?*e.receivedGlobalRow+1:0) && in.get()==e.receiveLocalRow,"MPI trace: encoded exchange mismatch");
        in.copy(e.sent);in.copy(e.received);
    }
    if(t.cell) {
        auto& c=*t.cell;require(in.get()==c.row && in.get()==c.column && in.get()==1,"MPI trace: encoded cell mismatch");c.captured=true;
        c.evaluation.before=in.byte();for(auto& n:c.evaluation.neighbors) n=in.byte();c.evaluation.neighborCount=in.byte();c.evaluation.after=in.byte();
    }
    require(in.offset==bytes.size(),"MPI trace: trailing rank payload");validate_rank_trace(t,w,q);return t;
}
void validate_trace_payload_header(const wire::Bytes& prefix,std::uint64_t length) {
    Reader in{prefix};const auto h=readHeader(in);
    require(length==trace_payload_size(h.trace.width,h.trace.height,h.processes,h.trace.selection),"MPI trace: payload length mismatch");
}
wire::Bytes encode_step_trace(const MpiStepTrace& t) {
    validate_step_trace(t);wire::Bytes out;out.reserve(trace_payload_size(t.width,t.height,static_cast<int>(t.ranks.size()),t.selection));
    for(auto v:{t.width,t.height,t.generationBefore,t.generationAfter,t.aliveBefore,t.aliveAfter,t.selection.row,t.selection.column,t.ranks.size()}) wire::put(out,v);
    for(const auto& r:t.ranks) {const auto bytes=encode_rank_trace(r,t.width,t.selection);out.insert(out.end(),bytes.begin(),bytes.end());}return out;
}
MpiStepTrace decode_step_trace(const wire::Bytes& bytes) {
    Reader in{bytes};auto h=readHeader(in);auto& t=h.trace;
    require(bytes.size()==trace_payload_size(t.width,t.height,h.processes,t.selection),"MPI trace: payload length mismatch");
    t.ranks.reserve(h.processes);
    for(int rank=0;rank<h.processes;++rank) {
        const auto p=partition_rows(t.height,h.processes,rank);const auto length=rankSize(t.width,p,t.selection);
        const wire::Bytes record(bytes.begin()+in.offset,bytes.begin()+in.offset+length);in.offset+=length;
        t.ranks.push_back(decode_rank_trace(record,t.width,p,t.selection));
    }
    validate_step_trace(t);return t;
}
}

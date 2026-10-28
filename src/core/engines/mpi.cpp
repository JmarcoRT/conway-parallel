#include "core/engines/mpi.hpp"
#include "core/engines/sequential.hpp"

#include <algorithm>
#include <climits>
#include <limits>
#include <stdexcept>

namespace conway::core {
double maximum_rank_milliseconds(double local,MPI_Comm comm) {
    double maximum=0;
    MPI_Reduce(&local,&maximum,1,MPI_DOUBLE,MPI_MAX,0,comm);
    return maximum;
}


std::size_t step_mpi(GameState& s, const mpi::RowPartition& part, MPI_Comm comm, mpi::RankStepTrace* trace) {
    // Validate collectively before any point-to-point exchange. Allocation and
    // complete input validation belong to initialization, not the step loop.
    int rank = 0, size = 0;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    const auto expected = mpi::partition_rows(part.globalHeight, size, rank);
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const bool shape = part.globalHeight > 0 && s.width > 0 &&
        s.width <= static_cast<std::size_t>(INT_MAX) / part.globalHeight &&
        s.height <= maximum - 2 && s.stride == s.width + 2 &&
        s.stride <= maximum / (s.height + 2);
    int valid = shape && part.rank == rank && part.size == size &&
        part.firstRow == expected.firstRow && part.rowCount == expected.rowCount &&
        s.height == part.rowCount && s.current.size() == s.stride * (s.height + 2) &&
        s.next.size() == s.current.size() && s.generation < maximum;
    if(trace) {
        try {mpi::validate_prepared_rank_trace(*trace,s.width);} catch(const std::exception&) {valid=0;}
        valid=valid && trace->partition.rank==part.rank && trace->partition.size==part.size &&
            trace->partition.globalHeight==part.globalHeight && trace->partition.firstRow==part.firstRow &&
            trace->partition.rowCount==part.rowCount && trace->generationBefore==s.generation;
    }
    int allValid = 0;
    MPI_Allreduce(&valid, &allValid, 1, MPI_INT, MPI_MIN, comm);
    if (!allValid) throw std::invalid_argument("step_mpi: estado o particion incompatible");

    const auto* currentAddress=s.current.data();const auto* nextAddress=s.next.data();
    if(trace) {
        std::copy(s.current.begin(),s.current.end(),trace->currentBefore.begin());
        trace->aliveBefore=s.aliveCount;
    }
    // MPI_PROC_NULL leaves receive buffers untouched: explicitly clear them on
    // every step, including after swapping buffers from earlier generations.
    std::fill_n(s.current.data(), s.stride, 0);
    std::fill_n(s.current.data() + (s.height + 1) * s.stride, s.stride, 0);
    if (!part.empty()) {
        const int above = part.firstRow == 0 ? MPI_PROC_NULL : rank - 1;
        const int below = part.endRow() == part.globalHeight ? MPI_PROC_NULL : rank + 1;
        const int count = static_cast<int>(s.width);
        for(int tag=0;tag<2;++tag) {
            auto* send=s.current.data()+(tag==0?1:s.height)*s.stride+1;
            auto* receive=s.current.data()+(tag==0?s.height+1:0)*s.stride+1;
            auto* event=trace?&trace->exchanges[tag]:nullptr;
            if(event && !event->sent.empty()) std::copy_n(send,count,event->sent.begin());
            MPI_Status status;
            MPI_Sendrecv(send,count,MPI_UINT8_T,tag==0?above:below,tag,
                         receive,count,MPI_UINT8_T,tag==0?below:above,tag,comm,
                         trace?&status:MPI_STATUS_IGNORE);
            if(event && !event->received.empty()) {
                int received=0;MPI_Get_count(&status,MPI_UINT8_T,&received);
                if(received!=count || status.MPI_SOURCE!=event->receivedFrom || status.MPI_TAG!=tag)
                    throw std::runtime_error("MPI trace: unexpected receive status");
                std::copy_n(receive,count,event->received.begin());
            }
        }
    }
    if(trace) std::copy(s.current.begin(),s.current.end(),trace->currentWithHalos.begin());
    step_sequential(s, kNoTrace, trace && trace->cell ? &*trace->cell : nullptr);
    if(trace) {
        std::copy(s.current.begin(),s.current.end(),trace->currentAfter.begin());
        trace->generationAfter=s.generation;trace->aliveAfter=s.aliveCount;
        trace->buffersSwapped=s.current.data()==nextAddress && s.next.data()==currentAddress;
    }
    const unsigned long long local = s.aliveCount;
    unsigned long long global = 0;
    MPI_Allreduce(&local, &global, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, comm);
    return static_cast<std::size_t>(global);
}

} // namespace conway::core

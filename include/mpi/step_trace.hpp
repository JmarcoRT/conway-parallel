#pragma once
#include "core/cell_probe.hpp"
#include "mpi/partition.hpp"
#include "mpi/protocol.hpp"
#include <optional>

namespace conway::mpi {
// Capture is bounded independently of ordinary boards and experimental batches.
inline constexpr std::size_t maxTraceCells=16384;
inline constexpr int maxTraceProcesses=128;
inline constexpr std::uint64_t maxTraceBytes=8u*1024u*1024u;
inline constexpr int noPeer=-1; // Portable wire representation of MPI_PROC_NULL.
struct TraceRequest {
    std::size_t expectedGeneration{}, row{}, column{}; // GLOBAL, zero based.
};
struct HaloExchange {
    int tag{}, sendTo{noPeer}, receivedFrom{noPeer};
    // Global row indexes: absent for MPI_PROC_NULL, which transmits no row.
    std::optional<std::size_t> sentGlobalRow, receivedGlobalRow;
    // 0 = upper halo, rowCount+1 = lower halo (local padded row).
    std::size_t receiveLocalRow{};
    std::vector<std::uint8_t> sent, received;
};
struct RankStepTrace {
    RowPartition partition;
    TraceRequest selection; // Same request as the aggregate; not duplicated on the wire.
    std::size_t generationBefore{}, generationAfter{}, aliveBefore{}, aliveAfter{};
    bool buffersSwapped{false};
    // Full padded buffers, stride=width+2, including side columns and halos.
    // currentBefore: BEFORE halo clearing; halo contents can be stale.
    // currentWithHalos: actual read buffer after both Sendrecv calls.
    // currentAfter: old NEXT after computation and swap; its halos are NOT
    // valid for the new generation. Old CURRENT (now NEXT) equals currentWithHalos.
    std::vector<std::uint8_t> currentBefore, currentWithHalos, currentAfter;
    // Local program order only, exactly two calls for a nonempty rank, none otherwise.
    std::vector<HaloExchange> exchanges;
    std::optional<core::CellProbe> cell;
};
struct MpiStepTrace {
    std::size_t width{}, height{}, generationBefore{}, generationAfter{}, aliveBefore{}, aliveAfter{};
    TraceRequest selection;
    // Indexed by rank; NOT global chronological order. Includes empty ranks.
    std::vector<RankStepTrace> ranks;
};
// Exact serialized size, validates dimensions/P/selection/generation and policy
// BEFORE allocating capture buffers. Does not inspect or evolve a board.
std::size_t trace_payload_size(std::size_t width,std::size_t height,int processes,const TraceRequest& request);
std::size_t rank_trace_payload_size(std::size_t width,const RowPartition& part,const TraceRequest& request);
RankStepTrace prepare_rank_trace(std::size_t width,const RowPartition& part,const TraceRequest& request);
// Allocation-free validation of policy, buffers, selection and message metadata BEFORE stepping.
void validate_prepared_rank_trace(const RankStepTrace& rank,std::size_t width);
void validate_rank_trace(const RankStepTrace& rank,std::size_t width,const TraceRequest& request);
void validate_step_trace(const MpiStepTrace& trace);
// Fixed-size-per-configuration records: length prefixes cannot request arbitrary allocations.
wire::Bytes encode_rank_trace(const RankStepTrace& rank,std::size_t width,const TraceRequest& request);
RankStepTrace decode_rank_trace(const wire::Bytes& bytes,std::size_t width,const RowPartition& part,const TraceRequest& request);
void validate_trace_payload_header(const wire::Bytes& prefix,std::uint64_t length);
wire::Bytes encode_step_trace(const MpiStepTrace& trace);
MpiStepTrace decode_step_trace(const wire::Bytes& bytes);
}

#pragma once

#include "core/game_state.hpp"
#include "mpi/partition.hpp"
#include "mpi/step_trace.hpp"
#include <mpi.h>
#include <cstddef>

namespace conway::core {
// Collective timing aggregation, result defined on rank 0; outside batch interval.
double maximum_rank_milliseconds(double local, MPI_Comm comm);

// Collective on comm, whose rank/size must match part. State contains exactly
// part's rows. All ranks, including empty partitions, call in the same order.
// Global dimensions/generation must agree across ranks; cells are binary and
// column halos zero, as produced by make_local_state_from_rows. Width and the
// global cell count must fit int for the prototype's Scatterv/Gatherv path.
// Exchanges current halos, reuses the sequential traversal and advances once.
// state.aliveCount remains LOCAL; the return value is the GLOBAL population.
// Optional capture must be prepared on EVERY rank with the SAME request before entering this call.
// No capture allocation or transport occurs when trace is null.
// Uses a dedicated communicator (or otherwise exclusively owned message tags).
std::size_t step_mpi(GameState& state, const mpi::RowPartition& part,
                     MPI_Comm comm, mpi::RankStepTrace* trace = nullptr);

} // namespace conway::core

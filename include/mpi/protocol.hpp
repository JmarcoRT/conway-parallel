#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

namespace conway::mpi::wire {
using Bytes = std::vector<std::uint8_t>;
using Deadline = std::chrono::steady_clock::time_point;
enum Type : std::uint64_t { Ready=1, Load, Advance, Snapshot, Close, Reply, Closed, Error, RunBatch, BatchResult, TraceStep, TraceResult };
inline constexpr std::uint64_t version = 3;
inline constexpr std::uint64_t maxPayload = 64u * 1024u * 1024u;
struct Frame { std::uint64_t type{}, session{}, request{}; Bytes payload; };
// Called after the fixed header and BEFORE allocating/reading its payload.
using HeaderCheck = std::function<void(const Frame&, std::uint64_t)>;
void put(Bytes& out, std::uint64_t value);
std::uint64_t get(const Bytes& in, std::size_t& offset);
void send(int fd, const Frame& frame, Deadline deadline, const std::function<void()>& tick = {});
Frame receive(int fd, Deadline deadline, const std::function<void()>& tick = {},
              const HeaderCheck& check = {});
} // namespace conway::mpi::wire

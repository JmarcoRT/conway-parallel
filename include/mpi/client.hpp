#pragma once
#include "input/input.hpp"
#include "mpi/protocol.hpp"
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

namespace conway::mpi {
struct ClientError : std::runtime_error {
    bool cleanupConfirmed;
    ClientError(std::string message,bool cleaned):std::runtime_error(std::move(message)),cleanupConfirmed(cleaned) {}
};
struct ClientOptions {
    std::string launcher;
    std::string worker;
    int processes{1};
    // Explicit caller policy only. The client NEVER adds oversubscription.
    std::vector<std::string> launcherArguments;
    std::chrono::milliseconds startupTimeout{10000}, operationTimeout{5000}, closeTimeout{3000};
};
struct BatchMetrics { double maximumMs{}; std::size_t generation{}; int processes{}; std::string runtime; };
struct RemoteSnapshot { InputData input; std::size_t generation{}, aliveCount{}; };
// Synchronous API intended for an application worker thread, never Render().
// try_lock rejects concurrent requests; failures invalidate and clean the job.
class Client {
public:
    explicit Client(ClientOptions options);
    ~Client();
    Client(const Client&)=delete;
    Client& operator=(const Client&)=delete;
    void load(const InputData& input, std::size_t generation=0);
    void advance(std::size_t steps=1);
    BatchMetrics runBatch(std::size_t steps, std::size_t expectedGeneration=0);
    RemoteSnapshot snapshot();
    bool cleanupConfirmed() const noexcept { return cleanupConfirmed_; }
    void close();
    const std::vector<int>& workerPids() const noexcept { return workers_; }
    int launcherPid() const noexcept { return pid_; }
    int declaredSlots() const noexcept { return declaredSlots_; }
    const std::string& resourceDirectory() const noexcept { return directory_; }
private:
    wire::Frame exchange(std::uint64_t type, wire::Bytes payload, std::uint64_t expected,
                         wire::Deadline deadline);
    void tick(bool allowLauncherExit=false);
    void drain() noexcept;
    void cleanup() noexcept;
    int declaredSlots_{1};
    bool cleanupConfirmed_{false};
    ClientOptions options_;
    int fd_{-1}, listener_{-1}, log_{-1}, logWrite_{-1}, pid_{-1};
    std::uint64_t session_{}, request_{};
    std::vector<int> workers_;
    std::vector<int> workerHandles_;
    std::string directory_, socket_, diagnostic_;
    std::mutex mutex_;
};
} // namespace conway::mpi

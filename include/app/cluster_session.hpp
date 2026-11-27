#pragma once

#include "input/generated_input.hpp"
#include "mpi/window_trace.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace conway::app {

enum class ClusterPhase { Setup, Loading, Publishing, Connecting, Ready, Closing, Closed, Cancelled, Error };

struct ClusterSettings {
    std::string ip;
    std::string port{"8080"};
    std::string nodes{"2"};
};

struct ClusterMember {
    bool connected{};
    std::optional<int> rank;
    std::string hostname;
    std::string ip;
};

struct ClusterResult {
    std::uint64_t id{};
    std::size_t width{}, height{}, population{};
    std::optional<mpi::WindowTrace> trace;
    std::string error;
};

struct ClusterState {
    ClusterPhase phase{ClusterPhase::Setup};
    std::optional<ClusterSettings> defaults;
    std::vector<ClusterMember> members;
    bool planAvailable{};
    std::string message;
    std::shared_ptr<const ClusterResult> result;

    bool active() const;
};

class ClusterSession {
public:
    struct Reset {};
    using Operation = std::variant<InputData, input::GenerationConfig, Reset, mpi::WindowRequest>;
    explicit ClusterSession(std::function<void()> notify);
    ClusterState snapshot() const;
    void loadDefaults();
    std::string start(const ClusterSettings& settings);
    std::optional<std::uint64_t> submit(Operation operation);
    void close();

private:
    void supervise(std::stop_token stop, std::vector<std::string> arguments, bool defaults);
    void publish(const ClusterState& state);

    std::function<void()> notify_;
    mutable std::mutex mutex_;
    ClusterState state_;
    struct Job { std::uint64_t id; Operation operation; };
    std::optional<Job> queued_;
    std::uint64_t nextId_{};
    bool outstanding_{};
    std::jthread worker_;
};

std::string clusterClientCommand(const ClusterSettings& settings, int node);

}  // namespace conway::app

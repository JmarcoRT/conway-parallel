#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

struct ClusterState {
    ClusterPhase phase{ClusterPhase::Setup};
    std::optional<ClusterSettings> defaults;
    std::vector<ClusterMember> members;
    bool planAvailable{};
    std::string message;

    bool active() const;
};

class ClusterSession {
public:
    explicit ClusterSession(std::function<void()> notify);
    ClusterState snapshot() const;
    void loadDefaults();
    std::string start(const ClusterSettings& settings);
    void close();

private:
    void supervise(std::stop_token stop, std::vector<std::string> arguments, bool defaults);
    void publish(const ClusterState& state);

    std::function<void()> notify_;
    mutable std::mutex mutex_;
    ClusterState state_;
    std::jthread worker_;
};

std::string clusterClientCommand(const ClusterSettings& settings, int node);

}  // namespace conway::app

#include "app/cluster_session.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <utility>

extern char** environ;

namespace conway::app {
namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::optional<int> number(std::string_view text) {
    int value;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return {};
    return value;
}

std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> result;
    for (;;) {
        const auto tab = line.find('\t');
        result.emplace_back(line.substr(0, tab));
        if (tab == line.npos) return result;
        line.remove_prefix(tab + 1);
    }
}

class Process {
public:
    explicit Process(std::vector<std::string>& args) {
        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
            throw std::runtime_error("No se pudo abrir el canal local del clúster.");
        fd_ = sockets[0];
        const int child = sockets[1];
        const int flags = fcntl(fd_, F_GETFL);
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        const bool actionsReady = posix_spawn_file_actions_init(&actions) == 0;
        const bool attributesReady = posix_spawnattr_init(&attributes) == 0;
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        const bool started = flags >= 0 && fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == 0 &&
            actionsReady && attributesReady &&
            posix_spawn_file_actions_adddup2(&actions, child, STDIN_FILENO) == 0 &&
            posix_spawn_file_actions_adddup2(&actions, child, STDOUT_FILENO) == 0 &&
            posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
            posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0 &&
            posix_spawnattr_setpgroup(&attributes, 0) == 0 &&
            posix_spawnp(&pid_, argv.front(), &actions, &attributes, argv.data(), environ) == 0;
        if (actionsReady) posix_spawn_file_actions_destroy(&actions);
        if (attributesReady) posix_spawnattr_destroy(&attributes);
        ::close(child);
        if (!started) {
            ::close(fd_);
            throw std::runtime_error("No se pudo ejecutar python3 para iniciar el clúster.");
        }
    }

    ~Process() {
        // Keep ownership of the PID until the process group has been signalled.
        if (pid_ > 0) {
            siginfo_t info{};
            int result;
            do { result = waitid(P_PID, pid_, &info, WEXITED | WNOHANG | WNOWAIT); }
            while (result < 0 && errno == EINTR);
            if (result == 0) kill(-pid_, SIGKILL);
            while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
        }
        ::close(fd_);
    }

    int fd() const { return fd_; }

    void requestClose() const {
        static constexpr std::string_view command = "CLOSE\n";
        // Shutdown also delivers EOF if the command could not be written.
        send(fd_, command.data(), command.size(), MSG_NOSIGNAL);
        shutdown(fd_, SHUT_WR);
    }

    std::optional<int> exitStatus(bool confirmed) {
        siginfo_t info{};
        if (waitid(P_PID, pid_, &info, WEXITED | WNOHANG | WNOWAIT) != 0) {
            if (errno == EINTR) return {};
            throw std::runtime_error("No se pudo consultar el proceso del clúster.");
        }
        if (!info.si_pid) return {};
        if (!confirmed || info.si_code != CLD_EXITED || info.si_status != 0)
            kill(-pid_, SIGKILL);
        int status;
        pid_t result;
        do { result = waitpid(pid_, &status, 0); } while (result < 0 && errno == EINTR);
        if (result < 0) throw std::runtime_error("No se pudo recoger el proceso del clúster.");
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

private:
    int fd_{-1};
    pid_t pid_{-1};
};

class SessionDirectory {
public:
    ~SessionDirectory() {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }

    const std::string& create() {
        auto path = (std::filesystem::temp_directory_path() / "conway-cluster-XXXXXX").string();
        if (!mkdtemp(path.data()))
            throw std::runtime_error("No se pudo crear el directorio privado del clúster.");
        path_ = std::move(path);
        return path_;
    }

private:
    std::string path_;
};

std::string phaseName(const ClusterState& state) {
    if (state.phase == ClusterPhase::Loading) return "Configuración del clúster";
    if (state.phase == ClusterPhase::Closing) return "Cerrando sesión";
    if (state.phase == ClusterPhase::Ready) return "Grupo listo";
    return state.planAvailable ? "Conectando por MPI" : "Publicando plan";
}

bool applyMessage(ClusterState& state, const std::vector<std::string>& data) {
    const auto& type = data[0];
    if (type == "DEFAULTS" && data.size() == 4) {
        state.defaults = ClusterSettings{data[1], data[2], data[3]};
        return false;
    }
    if (type == "PLAN") {
        state.planAvailable = true;
        if (state.phase != ClusterPhase::Closing) state.phase = ClusterPhase::Connecting;
        return true;
    }
    if ((type == "JOIN" && data.size() == 2) || (type == "MEMBER" && data.size() == 5)) {
        const auto id = number(data[1]);
        if (!id || *id < 0 || static_cast<std::size_t>(*id) >= state.members.size())
            throw std::runtime_error("Nodo fuera del plan.");
        auto& member = state.members[*id];
        member.connected = true;
        if (type == "MEMBER") {
            const auto rank = number(data[2]);
            if (rank != id) throw std::runtime_error("Rank fuera del plan.");
            member.rank = rank;
            member.hostname = data[3];
            member.ip = data[4];
        }
        return type == "JOIN";
    }
    if (type == "READY") {
        for (const auto& member : state.members)
            if (!member.rank) throw std::runtime_error("Falta confirmar un rank.");
        if (!state.planAvailable) throw std::runtime_error("El plan aún no está disponible.");
        if (state.phase != ClusterPhase::Closing) state.phase = ClusterPhase::Ready;
        return true;
    }
    return false;
}

std::vector<std::string> pythonCommand() {
    std::vector<std::string> args{"python3", CONWAY_CLUSTER_SCRIPT};
    if (const char* config = std::getenv("CONWAY_CLUSTER_CONFIG"); config && *config)
        args.insert(args.end(), {"--config", config});
    return args;
}

std::filesystem::path clusterBinary() {
    if (const char* binary = std::getenv("CONWAY_CLUSTER_BIN"); binary && *binary) return binary;
    return std::filesystem::read_symlink("/proc/self/exe").parent_path() / "conway_cluster";
}

}  // namespace

bool ClusterState::active() const {
    return phase == ClusterPhase::Loading || phase == ClusterPhase::Publishing ||
        phase == ClusterPhase::Connecting || phase == ClusterPhase::Ready || phase == ClusterPhase::Closing;
}

ClusterSession::ClusterSession(std::function<void()> notify) : notify_(std::move(notify)) {}

ClusterState ClusterSession::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void ClusterSession::publish(const ClusterState& state) {
    {
        std::lock_guard lock(mutex_);
        state_ = state;
    }
    notify_();
}

void ClusterSession::loadDefaults() {
    if (snapshot().active()) return;
    auto args = pythonCommand();
    args.push_back("ui-defaults");
    publish({ClusterPhase::Loading, {}, {}, false, {}});
    worker_ = std::jthread([this, args = std::move(args)](std::stop_token stop) mutable {
        supervise(stop, std::move(args), true);
    });
}

std::string ClusterSession::start(const ClusterSettings& settings) {
    if (snapshot().active()) return {};
    in_addr address;
    if (inet_pton(AF_INET, settings.ip.c_str(), &address) != 1 || address.s_addr == INADDR_ANY)
        return "Introduce la IPv4 del servidor en ZeroTier.";
    const auto port = number(settings.port);
    if (!port || *port < 1 || *port > 65535) return "El puerto debe estar entre 1 y 65535.";
    const auto nodes = number(settings.nodes);
    if (!nodes || *nodes < 2 || *nodes > 1024) return "Indica entre 2 y 1024 nodos, incluido el servidor.";

    auto args = pythonCommand();
    args.insert(args.end(), {"ui-server", "--server-ip", settings.ip, "--http-port", settings.port,
                             "--nodes", settings.nodes});
    std::vector<ClusterMember> members(*nodes);
    members.front().ip = settings.ip;
    publish({ClusterPhase::Publishing, {}, std::move(members), false, {}});
    worker_ = std::jthread([this, args = std::move(args)](std::stop_token stop) mutable {
        supervise(stop, std::move(args), false);
    });
    return {};
}

void ClusterSession::close() {
    worker_.request_stop();
}

void ClusterSession::supervise(std::stop_token stop, std::vector<std::string> args, bool defaults) {
    auto state = snapshot();
    ClusterPhase terminal = ClusterPhase::Error;
    try {
        SessionDirectory directory;
        if (!defaults)
            args.insert(args.end(), {"--binary", clusterBinary().string(), "--work-dir", directory.create()});
        Process process(args);
        std::string pending;
        std::optional<Clock::time_point> closing;
        const auto defaultsDeadline = Clock::now() + 5s;
        for (;;) {
            if (stop.stop_requested() && !closing) {
                process.requestClose();
                if (!defaults) {
                    state.phase = ClusterPhase::Closing;
                    publish(state);
                }
                closing = Clock::now() + 8s;
            }
            if ((closing && Clock::now() >= *closing) || (defaults && Clock::now() >= defaultsDeadline))
                throw std::runtime_error("Plazo de limpieza local agotado.");

            pollfd reader{process.fd(), POLLIN, 0};
            const auto events = poll(&reader, 1, 50);
            if (events < 0 && errno != EINTR)
                throw std::runtime_error("No se pudo consultar el canal local del clúster.");
            if (events > 0) {
                char buffer[4096];
                const auto size = recv(reader.fd, buffer, sizeof(buffer), 0);
                if (size > 0) {
                    pending.append(buffer, static_cast<std::size_t>(size));
                    for (auto newline = pending.find('\n'); newline != pending.npos; newline = pending.find('\n')) {
                        const auto data = fields(std::string_view(pending).substr(0, newline));
                        pending.erase(0, newline + 1);
                        if (data[0] == "DONE" && data.size() == 3) {
                            terminal = data[1] == "CLOSED" ? ClusterPhase::Closed :
                                data[1] == "CANCELLED" ? ClusterPhase::Cancelled : ClusterPhase::Error;
                            state.message = data[2];
                        } else if (applyMessage(state, data)) {
                            publish(state);
                        }
                    }
                    if (pending.size() > 8192) throw std::runtime_error("Respuesta local demasiado larga.");
                    continue;
                }
                if (size < 0 && errno != EINTR && errno != EAGAIN)
                    throw std::runtime_error("Se perdió el canal local del clúster.");
                if (size == 0) {
                    const bool confirmed = defaults ? state.defaults.has_value() :
                        terminal == ClusterPhase::Closed || terminal == ClusterPhase::Cancelled;
                    if (const auto code = process.exitStatus(confirmed)) {
                        if (defaults && *code == 0 && state.defaults) terminal = ClusterPhase::Setup;
                        if (*code != 0) terminal = ClusterPhase::Error;
                        break;
                    }
                    std::this_thread::sleep_for(20ms);
                }
            }
        }
        if (terminal == ClusterPhase::Error && state.message.empty())
            throw std::runtime_error("El proceso local terminó sin confirmar el resultado.");
    } catch (const std::exception& error) {
        terminal = ClusterPhase::Error;
        state.message = phaseName(state) + ": " + error.what();
        if (!defaults) state.message += " Cierre de clientes no confirmado.";
    }
    state.phase = terminal;
    publish(state);
}

std::string clusterClientCommand(const ClusterSettings& settings, int node) {
    in_addr address;
    const auto port = number(settings.port);
    if (inet_pton(AF_INET, settings.ip.c_str(), &address) != 1 || address.s_addr == INADDR_ANY ||
        !port || *port < 1 || *port > 65535) return {};
    return "python3 scripts/cluster/cluster.py client --interactive --server-ip " + settings.ip +
        " --http-port " + settings.port + " --node-id " + std::to_string(node);
}

}  // namespace conway::app

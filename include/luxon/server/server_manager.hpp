// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "metrics.hpp"
#include "handler_base.hpp"
#include "string_hash.hpp"
#include "logger.hpp"
#include "hookpoints.hpp"
#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
#include "ipc.hpp"
#endif
#ifdef LUXON_SERVER_ENABLE_SETTINGS_DATABASE
#include "settings_manager.hpp"
#endif
#ifndef LUXON_SERVER_POLL
#include "sock_selector.hpp"
#endif
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
#include "http_server.hpp"
#endif
#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
#include "command_restarter.hpp"
#endif
#ifdef LUXON_SERVER_MULTITHREADED
#include "sidethread.hpp"
#endif

#include <string>
#include <vector>
#include <list>
#include <queue>
#include <unordered_map>
#include <optional>
#include <memory>
#include <utility>
#include <functional>
#ifdef LUXON_SERVER_MULTITHREADED
#include <mutex>
#endif
#include <cstdint>
#include <luxon/enet_peer.hpp>
#ifdef LUXON_ENET_ENABLE_METRICS
#include <luxon/enet_metrics.hpp>
#endif
#include <commoncpp/timer.hpp>

namespace server {
class HandlerBase;
struct Peer;
struct PeerPersistent;
class App;

#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
template <typename T> using HandlerPtr = std::shared_ptr<T>;
#else
template <typename T> using HandlerPtr = std::unique_ptr<T>;
#endif

enum class ServerType { None, NameServer, MasterServer, GameServer };

struct ServerConfig {
    ServerType type = ServerType::None;
    uint16_t port = 0;

    bool allow_unsolicited = false, subprocess = false;

    std::string stun_server_host;
    uint16_t stun_server_port = 19302;
};

struct ServerEndpoint {
    ServerType type = ServerType::None;
    ServerProtocol protocol{};
    std::string address;
};

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
struct HttpServerConfig {
    bool enabled = false;
    std::string address = "0.0.0.0";
    uint16_t port = 5088;
};
#endif

///
/// \brief Runtime configuration for ServerManager
/// Can be built directly in C++ when embedding/loading the server as a shared library.
///
struct ServerManagerConfig {
    std::vector<ServerConfig> servers;
    std::vector<ServerEndpoint> endpoints;
    bool enable_ipv6 = true;
    unsigned max_connections = 0;
    ///
    /// \brief Maximum peers per game
    /// Values >= 255 are normalized to the legacy internal value 0 when applied.
    ///
    unsigned max_game_peers = 0;

    ///
    /// \brief Tick time budget
    /// Maximum amount of time run_once is allowed to take to service servers/peers.
    /// Lower value means more clients can safely be serviced at once, but processing latency will increase faster.
    ///
    uint32_t tick_time_budget = 2000;

#ifdef LUXON_SERVER_ENABLE_SETTINGS_DATABASE
    ///
    /// \brief Settings database file path
    /// Settings database will be initialized/read from this path.
    ///
    std::string settings_database_path;
#endif

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    std::optional<HttpServerConfig> http;
#endif

    ///
    /// \brief Add a listening server
    ///
    ServerManagerConfig& add_server(ServerType type, uint16_t port) {
        servers.push_back({type, port});
        return *this;
    }

    ///
    /// \brief Add a listening server and a matching external UDP endpoint
    ///
    ServerManagerConfig& add_server(ServerType type, uint16_t port, std::string external_udp_address) {
        servers.push_back({type, port});
        endpoints.push_back({type, ServerProtocol::UDP, std::move(external_udp_address)});
        return *this;
    }

    ///
    /// \brief Add an externally reachable endpoint
    ///
    ServerManagerConfig& add_endpoint(ServerType type, ServerProtocol protocol, std::string address) {
        endpoints.push_back({type, protocol, std::move(address)});
        return *this;
    }

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    ///
    /// \brief Enable embedded HTTP server
    ///
    ServerManagerConfig& enable_http(std::string address = "0.0.0.0", uint16_t port = 5088) {
        http = HttpServerConfig{true, std::move(address), port};
        return *this;
    }

    ///
    /// \brief Disable embedded HTTP server
    ///
    ServerManagerConfig& disable_http() {
        http.reset();
        return *this;
    }
#endif
};

class ServerManager {
    struct ScheduledTask {
        unsigned execution_time;
        std::function<void()> cb;
        bool operator>(const ScheduledTask& other) const { return execution_time > other.execution_time; }
    };

public:
    std::unordered_map<std::pair<std::string, std::string>, std::weak_ptr<App>, StringPairHasher> apps;
    std::vector<std::unique_ptr<PeerPersistent>> peer_persistent_data;
    std::vector<ServerEndpoint> endpoints;
#ifdef LUXON_SERVER_ENABLE_SETTINGS_DATABASE
    std::optional<SettingsManager> settings_manager;
    std::string settings_database_path;
#endif

#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
    Hookpoints hookpoints;
#endif

private:
#ifndef LUXON_SERVER_POLL
    SockSelector sock_selector_;
#endif

    std::shared_ptr<logger> log_;
    std::vector<ServerConfig> configs_;
    std::unordered_map<uint16_t, enet::EnetServer> servers_;
    decltype(servers_)::iterator next_server_it_;
    std::list<HandlerPtr<HandlerBase>> connections_;
    std::vector<std::shared_ptr<Game>> external_games_;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<ScheduledTask>> scheduled_tasks_;
    std::queue<std::move_only_function<void()>> main_loop_calls_;
#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
    IPC parent_ipc_;
    IPC *ipc_broadcast_skip_{};
    bool is_subprocess_{};
    std::unordered_map<uint16_t, IPC> subprocesses_;
#endif
#ifdef LUXON_SERVER_MULTITHREADED
    std::mutex main_loop_calls_mutex_;
#endif

    common::Timer startup_time_;
    common::Timer last_slow_update_;

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    std::optional<HttpServerConfig> http_config_;
    std::optional<HttpServer> http_server_;
#endif

#ifdef LUXON_ENET_ENABLE_METRICS
    enet::Metrics enet_metrics_;
    common::Timer enet_metrics_last_tick_;
#endif

#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
    std::unique_ptr<CommandRestarter> active_command_restarter_;
    bool active_command_restarter_allowed_ = false;
#endif

    bool running_ = true;

    bool enable_ipv6_ = true;
    unsigned max_connections_ = 0;
    uint8_t max_game_peers_ = 0;
    uint32_t tick_time_budget_ = 2000;

    void setup();
#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
    void setup_subprocess(const ServerConfig& config);
#endif
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    void setup_http_server();
#endif

#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
    void process_child_ipc_message(IPC& sender, const luxon::ser::Message& msg);
    void process_parent_ipc_message(IPC& sender, const luxon::ser::Message& msg);
    void process_ipc_event(const ser::EventMessage& event_msg);
#endif

    void run_scheduled_tasks();
    void stun_keepalive(enet::EnetServer& server, uint16_t port);

public:
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    Metric busy_time, idle_time;
#endif

    ///
    /// \brief Construct manager from YAML config file
    ///
    explicit ServerManager(const std::string& config_file);

    ///
    /// \brief Construct manager directly from C++ configuration
    ///
    explicit ServerManager(ServerManagerConfig config
#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
                           ,
                           IPC&& ipc = {}
#endif
    );

#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
    ///
    /// \brief Construct manager from IPC child fd
    /// \note Configuration will be received from parent via IPC
    ///
    explicit ServerManager(IPC&& ipc);

    ///
    /// \brief Receive ServerManagerConfig from parent
    ///
    static ServerManagerConfig receive_config_from_ipc(IPC& ipc);
#endif

    ///
    /// \brief Load ServerManagerConfig from YAML file
    ///
    static ServerManagerConfig load_config_from_file(const std::string& config_file);

    ///
    /// \brief Parse ServerManagerConfig from YAML contents
    ///
    static ServerManagerConfig parse_config(const std::string& config_contents);

    ///
    /// \brief Runs server until stop() is called
    ///
    void run();
    ///
    /// \brief Ticks once
    /// \return True if the server should continue running, false if stop() was called
    /// \note Non-blocking if polling is enabled
    ///
    bool run_once();
    ///
    /// \brief Signals the server to stop running asap
    /// \note It might take the server up to about 125 milliseconds to stop if polling is disabled
    ///
    void stop() { running_ = false; }

    ///
    /// \brief Schedules a function to be called from main loop
    /// \param delay_ms Delay in milliseconds
    /// \param callback Function to be called
    ///
    void add_scheduled_task(unsigned delay_ms, std::function<void()>&& callback) {
        unsigned target_time = startup_time_.get() + delay_ms;
        scheduled_tasks_.push({target_time, std::move(callback)});
    }

    ///
    /// \brief Retrieves an application instance based on the provided info
    /// \param info Identifying information for the target application
    /// \return Shared pointer to the application
    ///
    std::shared_ptr<App> get_app(const AppInfo& info);

    ///
    /// \brief Retrieves a lobby instance within a specific application
    /// \param app Parent application hosting the lobby
    /// \param info Identifying information for the target lobby
    /// \return Shared pointer to the lobby
    ///
    std::shared_ptr<Lobby> get_lobby(App& app, const LobbyInfo& info);

    ///
    /// \brief Retrieves a game instance within a specific lobby
    /// \param lobby Parent lobby hosting the game
    /// \param info Identifying information for the target game
    /// \return Expected containing a shared pointer to the game on success, or an error message string on failure
    ///
    std::expected<std::shared_ptr<Game>, std::string> get_game(Lobby& lobby, const GameInfo& info);

#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
    ///
    /// \brief Broadcasts a serialized message via IPC to parent and/or child processes
    /// \param message The serialized message payload to send
    /// \param parent True to transmit the message to the parent process
    /// \param children True to transmit the message to all child subprocesses
    /// \param skip Optional IPC connection pointer to exclude from the broadcast (e.g., the original sender)
    ///
    void ipc_broadcast(const ser::Message& message, bool parent, bool children);

    ///
    /// \brief Broadcasts a serialized message via IPC to all connected processes (parent and children)
    /// \param message The serialized message payload to send
    /// \param skip Optional IPC connection pointer to exclude from the broadcast (e.g., the original sender)
    ///
    void ipc_broadcast(const ser::Message& message) { return ipc_broadcast(message, true, true); }
#endif

    std::string_view get_static_endpoint_address_str(std::string_view address);

    ///
    /// \brief Gets the external address of a random server of given type
    /// \param server_type Type of server to get
    /// \param server_proto Protocol of server to get
    /// \return Externally reachable address of server, e.g. "104.18.26.120:5058"
    ///
    const ServerEndpoint& get_endpoint_of(ServerType server_type, ServerProtocol server_proto);

    ///
    /// \brief Gets a list of active connections to this instance
    /// \return Linked list of handlers representing a connection
    ///
    const std::list<HandlerPtr<HandlerBase>>& get_connections() { return connections_; }

    ///
    /// \brief Counts connections to servers on this instance of any type
    /// \return Amount of connections
    ///
    size_t get_connection_count() { return connections_.size(); }
    ///
    /// \brief Counts connections to servers on this instance with handler of or derived from given type
    /// \return Amount of connections
    ///
    template <class HandlerT> size_t get_connection_count() {
        size_t fres = 0;
        for (const auto& connection : connections_)
            fres += !!dynamic_cast<HandlerT *>(connection.get());
        return fres;
    }
    ///
    /// \brief Gets the maximum allowed connections
    ///
    unsigned get_max_connections() const { return max_connections_; }
    ///
    /// \brief Gets the maximum allowed peers per game
    ///
    uint8_t get_max_game_peers() const { return max_game_peers_; }

    ///
    /// \brief Gets list of servers
    ///
    auto get_servers() {
        std::vector<std::pair<uint16_t, enet::EnetServer *>> fres;
        fres.reserve(servers_.size());
        for (auto& [port, server] : servers_)
            fres.push_back({port, &server});
        return fres;
    }
#ifdef LUXON_ENET_ENABLE_METRICS

    ///
    /// \brief Gets all metrics exposed by ENet
    ///
    const enet::Metrics& get_enet_metrics() const { return enet_metrics_; }
#endif

#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
    ///
    /// \brief Gets the restarter of the current command and cancel processing as soon as possible
    ///
    std::unique_ptr<CommandRestarter> take_command_restarter() {
        return active_command_restarter_allowed_ ? std::exchange(active_command_restarter_, nullptr) : nullptr;
    }
#endif

    ///
    /// \brief Prevents current command from being restarted
    ///
    bool mark_command_committed() {
        if (should_abort_active_command())
            return false;
#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
        active_command_restarter_allowed_ = false;
#endif
        return true;
    }

    ///
    /// \brief Checks if processing of current command is to be aborted
    ///
    bool should_abort_active_command() const {
#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
        return active_command_restarter_ == nullptr;
#else
        return false;
#endif
    }

    static std::function<void(int fd)> handle_start_subprocess;
};
} // namespace server

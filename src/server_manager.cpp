// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

/*
 * MACRO CONFIGURATION
 * ---------------------------
 * This file relies on the following preprocessor macros to control build variants,
 * memory management strategies, concurrency features, and I/O models:
 *
 * LUXON_SERVER_ENABLE_COROUTINES
 * Enables the asynchronous coroutine architecture.
 * - Switches connection handlers (HandlerPtr) from std::unique_ptr to std::shared_ptr
 *   to support the shared lifecycle ownership required by detached coroutines.
 * - Wraps incoming ENet packet handling (HandleENetCommand) inside a coroutine
 *   (minicoro), allowing handlers to yield execution without blocking the server thread.
 * - Exposes non-blocking execution yield utilities like `delay` and thread-safe main-loop queues.
 *
 * LUXON_SERVER_MULTITHREADED
 * Toggles multithreading capabilities and thread-safe synchronization.
 * - Enables the `main_loop_calls_` queue and protects it via `main_loop_calls_mutex_`
 *   to safely dispatch tasks from arbitrary threads back onto the main loop thread.
 * - When combined with LUXON_SERVER_ENABLE_COROUTINES, enables `call_in_side_thread`
 *   and `call_in_new_thread` to offload heavy tasks and safely resume coroutines upon completion.
 *
 * LUXON_SERVER_ENABLE_SETTINGS_DATABASE
 * Toggles embedded settings database management.
 * - Parses the "SettingsDatabase" path string from the root YAML configuration map.
 * - Conditionally initializes the `settings_manager` object lifecycle within the server manager.
 *
 * LUXON_SERVER_ENABLE_HOOKPOINTS
 * Toggles server event interception extensions.
 * - Instantiates the `hookpoints` structure inside the ServerManager to allow external
 *   modules or plugins to register lifecycle and packet hooks.
 *
 * LUXON_ENET_ENABLE_METRICS
 * Toggles granular network telemetry and telemetry tracking.
 * - Instantiates internal `enet::Metrics` tracking objects and interval timers.
 * - Flushes and ticks tracking data periodically (~1000ms intervals) during slow server updates.
 * - Exposes metrics publicly via `get_enet_metrics()`.
 *
 * NDEBUG
 * Standard C++ macro for Release builds.
 * - If UNDEFINED (Debug build):
 *   1. Sets logger verbosity automatically to `trace` level.
 *   2. Enables deep payload inspection (`visualizer::print_ser_message` / `print_http_message`)
 *      and raw hex dumps for unrecognized ENet payloads to assist protocol debugging.
 *
 * LUXON_SERVER_ENABLE_WEBSERVER
 * Toggles the embedded HTTP server component.
 * - Parses the "HTTP" section of the YAML configuration map.
 * - Instantiates the `http_server_` instance and handles automated lifecycle updates.
 * - Collects performance diagnostics (`idle_time`, `busy_time` metrics) per loop tick.
 * - Hooks HTTP socket file descriptors directly into the main read selector (if not polling).
 *
 * LUXON_SERVER_POLL
 * Determines the network I/O multiplexing strategy.
 * - If DEFINED: Uses a manual polling model. The `sock_selector_` logic and its automated
 *   descriptor registration blocks are skipped entirely.
 * - If UNDEFINED: Uses an event-driven model via `sock_selector_` (wrapping select/epoll).
 *   Registers native server handles and embedded HTTP file descriptors to sleep efficiently
 *   until socket readability is confirmed.
 */

#include "server_manager.hpp"
#include "global.hpp"
#include "platform.hpp"
#include "peer.hpp"
#include "logger.hpp"
#include "handler_nameserver.hpp"
#include "handler_masterserver.hpp"
#include "handler_gameserver.hpp"
#include "yaml.hpp"

#include <iostream>
#include <string_view>
#include <fstream>
#include <sstream>
#include <format>
#include <random>
#include <exception>
#include <stdexcept>
#include <algorithm>
#ifdef LUXON_SERVER_MULTITHREADED
#include <thread>
#endif
#ifdef LUXON_SERVER_ENABLE_COROUTINES
#include <minicoropp.hpp>
#endif
#include <luxon/ser_gp_binary_v18.hpp>
#include <luxon/ser_encryption.hpp>
#include <luxon/visualizer.hpp>
#include <tracy/Tracy.hpp>

namespace server {
namespace {
ServerType StringToServerType(const std::string& str) {
    if (str == "NameServer")
        return ServerType::NameServer;
    if (str == "MasterServer")
        return ServerType::MasterServer;
    if (str == "GameServer")
        return ServerType::GameServer;

    throw std::runtime_error("Unknown ServerType: " + str);
}

ServerProtocol StringToEndpointProtocol(const std::string& str) {
    if (str == "UDP")
        return ServerProtocol::UDP;
    if (str == "TCP")
        return ServerProtocol::TCP;
    if (str == "WebSocket")
        return ServerProtocol::WebSocket;

    throw std::runtime_error("Unknown protocol: " + str);
}

std::string LoadFile(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f)
        throw std::runtime_error("Failed to open config file: " + filename);
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

std::string_view ServerTypeToString(ServerType type) {
    switch (type) {
    case ServerType::NameServer:
        return "NameServer";
    case ServerType::MasterServer:
        return "MasterServer";
    case ServerType::GameServer:
        return "GameServer";
    default:
        return "Unknown???";
    }
}

HandlerPtr<HandlerBase> ServerTypeToHandler(ServerType type, ServerManager& server_man, std::shared_ptr<Peer>&& peer) {
    switch (type) {
    case ServerType::NameServer:
        return HandlerPtr<NameServerHandler>(new NameServerHandler(server_man, peer));
    case ServerType::MasterServer:
        return HandlerPtr<MasterServerHandler>(new MasterServerHandler(server_man, peer));
    case ServerType::GameServer:
        return HandlerPtr<GameServerHandler>(new GameServerHandler(server_man, peer));
    default:
        return nullptr;
    }
}

bool IsServerTypeSection(std::string_view key) { return key == "NameServer" || key == "MasterServer" || key == "GameServer"; }

uint8_t NormalizeMaxGamePeers(unsigned value) {
    uint8_t normalized = static_cast<uint8_t>(std::min<unsigned>(value, 255));
    if (normalized == 255)
        normalized = 0;
    return normalized;
}

template <typename Fn> void ForEachSequenceItem(Yaml::Node& section, Fn&& fn) {
    if (!section.IsSequence())
        return;

    for (auto itemIt = section.Begin(); itemIt != section.End(); itemIt++)
        fn((*itemIt).second);
}

void ParseServerSection(ServerManagerConfig& config, ServerType current_type, Yaml::Node& section) {
    struct {
        bool allow_unsolicited = false;
        std::string stun_host;
        uint16_t stun_port = 19302;
    } state;

    ForEachSequenceItem(section, [&](Yaml::Node& item) {
        if (!item["allow_unsolicited"].IsNone())
            state.allow_unsolicited = item["allow_unsolicited"].As<bool>();

        if (!item["stun_server"].IsNone()) {
            Yaml::Node& stun = item["stun_server"];

            if (stun.IsScalar()) {
                state.stun_host = stun.As<std::string>();
            } else if (stun.IsSequence()) {
                ForEachSequenceItem(stun, [&](Yaml::Node& entry) {
                    if (!entry["host"].IsNone())
                        state.stun_host = entry["host"].As<std::string>();

                    if (!entry["port"].IsNone())
                        state.stun_port = entry["port"].As<uint16_t>();
                });
            } else {
                throw std::runtime_error("stun_server must be either a string or a sequence");
            }
        }

        if (!item["port"].IsNone()) {
            config.servers.push_back({current_type, item["port"].As<uint16_t>(), state.allow_unsolicited, std::move(state.stun_host), state.stun_port});
            state = {};
        }

        if (!item["address"].IsNone())
            config.endpoints.push_back({current_type, ServerProtocol::UDP, item["address"].As<std::string>()});
    });
}

void ParseExternalSection(ServerManagerConfig& config, Yaml::Node& section) {
    ForEachSequenceItem(section, [&](Yaml::Node& item) {
        ServerType ext_type = ServerType::None;
        ServerProtocol ext_proto = ServerProtocol::UDP;
        std::string ext_addr;
        bool addr_found = false;

        if (!item["type"].IsNone())
            ext_type = StringToServerType(item["type"].As<std::string>());

        if (!item["protocol"].IsNone())
            ext_proto = StringToEndpointProtocol(item["protocol"].As<std::string>());

        if (!item["address"].IsNone()) {
            ext_addr = item["address"].As<std::string>();
            addr_found = true;
        }

        if (ext_type != ServerType::None && addr_found)
            config.endpoints.push_back({ext_type, ext_proto, std::move(ext_addr)});
    });
}

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
void ParseHttpSection(ServerManagerConfig& config, Yaml::Node& section) {
    HttpServerConfig http_cfg;
    bool seen_any_item = false;

    ForEachSequenceItem(section, [&](Yaml::Node& item) {
        seen_any_item = true;

        if (!item["active"].IsNone())
            http_cfg.enabled = item["active"].As<bool>();
        if (!item["address"].IsNone())
            http_cfg.address = item["address"].As<std::string>();
        if (!item["port"].IsNone())
            http_cfg.port = item["port"].As<uint16_t>();
    });

    if (seen_any_item)
        config.http = std::move(http_cfg);
}
#endif

template <typename T> T *GetRawPointer(T *ptr) { return ptr; }

template <typename T> T *GetRawPointer(const std::shared_ptr<T>& ptr) { return ptr.get(); }

template <typename T> T *GetRawPointer(const std::unique_ptr<T>& ptr) { return ptr.get(); }
} // namespace

ServerManagerConfig ServerManager::load_config_from_file(const std::string& config_file) { return parse_config(LoadFile(config_file)); }

ServerManagerConfig ServerManager::parse_config(const std::string& config_contents) {
    Yaml::Node root;

    try {
        Yaml::Parse(root, config_contents);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("YAML Parsing failed: ") + e.what());
    }

    if (!root.IsMap())
        throw std::runtime_error("Root of config must be a map");

    ServerManagerConfig config;

    for (auto it = root.Begin(); it != root.End(); it++) {
        std::string key = (*it).first;
        Yaml::Node& section = (*it).second;

        if (IsServerTypeSection(key)) {
            ParseServerSection(config, StringToServerType(key), section);
        } else if (key == "External") {
            ParseExternalSection(config, section);
        } else if (key == "EnableIPv6") {
            if (!section.IsNone())
                config.enable_ipv6 = section.As<bool>();
        } else if (key == "MaxConnections" || key == "CCU") {
            if (!section.IsNone())
                config.max_connections = section.As<unsigned>();
        } else if (key == "MaxGamePeers") {
            if (!section.IsNone())
                config.max_game_peers = section.As<unsigned>();
        } else if (key == "TickTimeBudget") {
            if (!section.IsNone())
                config.tick_time_budget = section.As<uint32_t>();
#ifdef LUXON_SERVER_ENABLE_SETTINGS_DATABASE
        } else if (key == "SettingsDatabase") {
            if (!section.IsNone())
                config.settings_database_path = section.As<std::string>();
#endif
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        } else if (key == "HTTP") {
            ParseHttpSection(config, section);
#endif
        }
    }

    return config;
}

ServerManager::ServerManager(const std::string& config_file) : ServerManager(load_config_from_file(config_file)) {}

ServerManager::ServerManager(ServerManagerConfig config) : endpoints(std::move(config.endpoints)) {
    log_ = create_logger("ServerManager");
#ifndef NDEBUG
    log_->set_level(log_level::trace);
#endif

    configs_ = std::move(config.servers);
    enable_ipv6_ = config.enable_ipv6;
    max_connections_ = config.max_connections;
    max_game_peers_ = NormalizeMaxGamePeers(config.max_game_peers);
    tick_time_budget_ = config.tick_time_budget;
    if (tick_time_budget_ == 0)
        tick_time_budget_ = ~tick_time_budget_;

#ifdef LUXON_SERVER_ENABLE_SETTINGS_DATABASE
    if (!config.settings_database_path.empty()) {
        log_->info("Using settings database!");
        settings_manager.emplace(config.settings_database_path);
    }
#endif

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    http_config_ = std::move(config.http);
#endif

    log_->info("Config looks alright, setting up accordingly");
    setup();
}

#ifdef LUXON_SERVER_ENABLE_COROUTINES
#ifdef LUXON_SERVER_MULTITHREADED
bool ServerManager::call_in_side_thread(const SideThreadPtr& side_thread, std::move_only_function<void()>&& fn) {
    if (!side_thread)
        return false;

    auto *coro = minicoro::Coroutine::current();
    if (!coro)
        return false;

    bool ok = false;
    side_thread->enqueue([&, this] {
        try {
            fn();
            ok = true;
        } catch (const std::exception& e) {
            log_->error("Unhandled exception in side thread: {}: {}", typeid(e).name(), e.what());
        } catch (...) {
            log_->error("Unknown unhandled exception in side thread!");
        }

        enqueue_in_main_loop([coro, this] {
            if (!coro->resume())
                log_->error("Failed to resume coroutine from side thread!");
        });
    });

    coro->yield();
    return ok;
}

bool ServerManager::call_in_new_thread(std::move_only_function<void()>&& fn) {
    auto *coro = minicoro::Coroutine::current();
    if (!coro)
        return false;

    bool ok = false;
    std::thread([&, this] {
        try {
            fn();
            ok = true;
        } catch (const std::exception& e) {
            log_->error("Unhandled exception in side thread: {}: {}", typeid(e).name(), e.what());
        } catch (...) {
            log_->error("Unknown unhandled exception in side thread!");
        }

        enqueue_in_main_loop([coro, this] {
            if (!coro->resume())
                log_->error("Failed to resume coroutine from side thread!");
        });
    }).detach();

    coro->yield();
    return ok;
}
#endif

bool ServerManager::delay(unsigned milliseconds) {
    auto *coro = minicoro::Coroutine::current();
    if (!coro)
        return false;

    add_scheduled_task(milliseconds, [coro, this] {
        if (!coro->resume())
            log_->error("Failed to resume coroutine from scheduled task!");
    });

    coro->yield();
    return true;
}
#endif

const std::string& ServerManager::get_endpoint_of(ServerType server_type, ServerProtocol server_proto) {
    ZoneScoped;
    std::vector<const std::string *> candidates;
    candidates.reserve(endpoints.size());
    // Collect all valid addresses for the requested type
    for (const auto& endpoint : endpoints)
        if (endpoint.type == server_type && endpoint.protocol == server_proto)
            candidates.push_back(&endpoint.address);
    // Handle cases where no config exists
    if (candidates.empty())
        throw std::runtime_error(std::format("No endpoint configuration found for {}", ServerTypeToString(server_type)));
    // Return a random address from the candidates
    static std::mt19937 generator{1234};
    std::uniform_int_distribution<size_t> distribution(0, candidates.size() - 1);
    return *candidates[distribution(generator)];
}

void ServerManager::run_scheduled_tasks() {
    ZoneScoped;
    // Check if queue is empty first to avoid segfaults on top()
    if (scheduled_tasks_.empty())
        return;
    const auto& task = scheduled_tasks_.top();
    if (task.execution_time < startup_time_.get()) {
        auto callback = task.cb;
        scheduled_tasks_.pop();
        callback();
    }
}

void ServerManager::stun_keepalive(enet::EnetServer& server, uint16_t port) {
    if (!server.keepalive_stun_binding())
        log_->warn("[STUN:{}] Failed to keep alive server STUN binding", port);

    add_scheduled_task(17500, std::bind(&ServerManager::stun_keepalive, this, std::ref(server), port));
}

void ServerManager::run() {
    // Main Service Loop
    running_ = true;
    do {
        run_once();
    } while (running_ && Platform::cooperate());
    running_ = true;
}

bool ServerManager::run_once() {
    ZoneScoped;
    {
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // Start idle performance timer
        const auto start_time = std::chrono::steady_clock::now();
#endif

#ifndef LUXON_SERVER_POLL
        // Run sock selector
        sock_selector_.run(125);
#endif

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // End idle performance timer
        const auto end_time = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

        // Store metric
        idle_time.add(static_cast<unsigned>(duration));
#endif
    }
    {
        ZoneScopedN("run_once_busy");
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // Start busy performance timer
        const auto start_time = std::chrono::steady_clock::now();
#endif

        // Check if slow update should be done
        const bool slow_update = last_slow_update_.get() > 250;
        if (slow_update)
            last_slow_update_.reset();
#ifdef LUXON_SERVER_ENABLE_COROUTINES
        // Run stuff that's queued to run in the main loop asap
        while (true) {
            // Get next callback safely
            std::move_only_function<void()> fn{};
            {
#ifdef LUXON_SERVER_MULTITHREADED
                std::scoped_lock L(main_loop_calls_mutex_);
#endif
                if (!main_loop_calls_.empty()) {
                    fn = std::move(main_loop_calls_.front());
                    main_loop_calls_.pop();
                }
            }
            if (fn)
                fn();
            else
                break;
        }
#endif

        // Dispatch and handle incoming application messages
        uint32_t remaining_timeout = tick_time_budget_;
        size_t servers_to_process = servers_.size();

        {
            ZoneScopedN("service_peers");

            // Round-robin over servers to prevent starvation
            while (servers_to_process > 0 && remaining_timeout > 0) {
                // Wrap around to beginning if end is hit
                if (next_server_it_ == servers_.end())
                    next_server_it_ = servers_.begin();

                auto& [port, server] = *next_server_it_;

                try {
                    // Service peers using whatever remains of global budget
                    if (!server.service_peers(remaining_timeout))
                        log_->warn("Queueing UDP datagrams on port {}!", port);
                } catch (const std::exception& e) {
                    log_->warn("Uncaught exception on port {}: {}", port, e.what());
                }

                // Move to the next server and decrement safety counter
                ++next_server_it_;
                --servers_to_process;
            }
        }

        if (servers_to_process > 0)
            log_->warn("Tick time budget exhausted! {} servers deferred to next tick.", servers_to_process);

        // Trigger updates
        if (slow_update) {
            ZoneScopedN("service_slow_updates");
#ifdef LUXON_ENET_ENABLE_METRICS
            // Tick enet metrics
            const auto enet_metrics_last_tick_ms = enet_metrics_last_tick_.get();
            if (enet_metrics_last_tick_ms > 1000) {
                const double enet_metrics_last_tick_s = double(enet_metrics_last_tick_ms) * 0.001;
                enet_metrics_last_tick_.reset();
                enet_metrics_.tick(enet_metrics_last_tick_s);
            }
#endif
            // Update connection handlers
            for (auto& connection : connections_) {
                try {
                    connection->HandleSlowUpdate();
                } catch (const std::exception& e) {
                    auto& peer = *connection->get_peer();
                    log_->warn("Disconnecting due to uncaught exception in slow update: {}", e.what());
                    peer.disconnect();
                }
            }
        }

        // Run scheduled tasks
        run_scheduled_tasks();
#ifdef LUXON_SERVER_ENABLE_WEBSERVER

        // Update HTTP server
        if (http_server_)
            http_server_->service_now();

        // End busy performance timer
        const auto end_time = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

        // Store metric
        busy_time.add(static_cast<unsigned>(duration));
#endif
    }

    FrameMark;
    return running_;
}

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
void ServerManager::setup_http_server() {
    if (!http_config_ || !http_config_->enabled) {
        log_->debug("HTTP Server disabled in config.");
        return;
    }

    log_->info("Initializing HTTP Server on port {}", http_config_->port);
    http_server_.emplace(*this);
#ifndef LUXON_SERVER_POLL
    http_server_->on_create_fd =
        std::bind(&SockSelector::add_read_fd, &sock_selector_, std::placeholders::_1, [this](int fd) { http_server_->service_later(fd); });
    http_server_->on_delete_fd = std::bind(&SockSelector::remove_read_fd, &sock_selector_, std::placeholders::_1);
#endif
    http_server_->bind(http_config_->address, http_config_->port);
}
#endif

void ServerManager::setup() {
    ZoneScoped;

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    setup_http_server();
#endif

    enet::EnetPeerConfig cfg;
    cfg.time_base = enet::EnetPeer::create_time_base();
    cfg.time_ping_interval_ms = 1000;
    cfg.disconnect_timeout_ms = 5000;

    // Create servers
    for (const auto& config : configs_) {
        // Create enet server and configure it
        log_->info("Setting up {} on port {}", ServerTypeToString(config.type), config.port);

        auto& server = servers_
                           .try_emplace(config.port, cfg
#ifdef LUXON_ENET_ENABLE_METRICS
                                        ,
                                        enet_metrics_
#endif
                                        )
                           .first->second;

        server.on_peer_connected = [this, &config](std::shared_ptr<enet::EnetPeer> enetPeer) {
            // Construct peer
            auto peer = std::make_shared<Peer>();
            peer->enet_peer = enetPeer;
            peer->log = create_logger(std::format("Peer {}@{}", enetPeer->peer_id(), enetPeer->remote_endpoint()->to_string()));
            peer->protocol = std::make_unique<ser::GpBinaryV18>(); // Default version
#ifndef NDEBUG
            peer->log->set_level(log_level::trace);
#endif
            peer->log->info("Peer {} constructed with {} handler", peer->enet_peer->peer_id(), ServerTypeToString(config.type));

            // Construct handler
            auto handler = ServerTypeToHandler(config.type, *this, std::move(peer));
            handler->set_allow_unsolicited(config.allow_unsolicited);

            // Handler pointer must be owning with plugins enabled to ensure no destruction while coroutine is active
#ifdef LUXON_SERVER_ENABLE_COROUTINES
            auto handler_ptr = handler;
#else
            auto *handler_ptr = handler.get();
#endif
            auto *raw_handler = GetRawPointer(handler_ptr);

            // Install callbacks
            enetPeer->on_log_message = [this, handler = handler_ptr](enet::LogLevel enet_level, std::string_view message) {
                // Convert log level
                log_level level;
                switch (enet_level) {
                case luxon::enet::LogLevel::Warning:
                    level = log_level::warn;
                    break;
                case luxon::enet::LogLevel::Error:
                    level = log_level::err;
                    break;
                }

                // Emit log message
                handler->get_peer()->log->log(level, "[ENet] {}", message);
            };

            enetPeer->on_state_changed = [this, handler = handler_ptr, raw_handler](enet::EnetConnectionState state) {
                try {
                    handler->HandleENetConnectionStateChange(state);
                } catch (const std::exception& e) {
                    auto& peer = *handler->get_peer();
                    peer.log->warn("Uncaught exception in ENet connect state change handler: {}", e.what());
                }

                if (state == enet::EnetConnectionState::Disconnected) {
                    handler->HandleDisconnect();
                    // Self-destruct handler, this will invalidate the pointer
                    connections_.remove_if([raw_handler](auto& v) { return v.get() == raw_handler; });
                }
            };

            enetPeer->on_payload_command = [this, handler = handler_ptr](enet::EnetCommand&& cmd) {
                auto& peer = handler->get_peer();
#ifndef NDEBUG
                peer->log->trace("Received message using mode {} on channel {}:", static_cast<int>(enet::FlagsToEnetDeliveryMode(cmd.header.flags)),
                                 cmd.header.channel_id);
                if (!visualizer::print_ser_message(cmd.get_payload(), 2, *peer->protocol)) {
                    if (!visualizer::print_http_message(cmd.get_payload(), 2)) {
                        peer->log->error("Message not understood!");
                        visualizer::helpers::print_hex_dump(cmd.get_payload(), 2);
                    }
                }
#endif
#ifdef LUXON_SERVER_ENABLE_COROUTINES
                if (!(new minicoro::Coroutine([handler, cmd = std::move(cmd), this](minicoro::Coroutine& coro) mutable {
                         // Make coroutine own itself
                         auto owned_coro =
                             std::unique_ptr<minicoro::Coroutine, std::function<void(minicoro::Coroutine *)>>(&coro, [this](minicoro::Coroutine *coro) {
                                 // Discard coroutine on main thread, where it can be destroyed safely
                                 enqueue_in_main_loop([coro] { delete coro; });
                             });
#endif

                         try {
                             handler->HandleENetCommand(std::move(cmd));
                         } catch (const std::exception& e) {
                             auto& peer = *handler->get_peer();
                             peer.log->critical("Disconnecting due to uncaught exception in ENet command handler: {}", e.what());
                             peer.disconnect();
                         }

#ifdef LUXON_SERVER_ENABLE_COROUTINES
                     }))->resume()) {
                    peer->log->critical("Disconnecting because ENet command handler coroutine couldn't be started");
                    peer->disconnect();
                }
#endif
            };

            // Add to connection list
            auto& handlerPtr = connections_.emplace_back(std::move(handler));

            // Tell handler that we're connected
            handlerPtr->HandleConnect();
        };

        server.on_stun_bind = [this, &config](enet::EnetEndpoint&& ep) {
            log_->info("[STUN:{}] NAT punch complete  --> {} <-- ", config.port, ep.to_string());
        };

        // Make server ready for listening
        log_->info("Starting {} on port {}", ServerTypeToString(config.type), config.port);

        if (!server.bind(config.port, enable_ipv6_)) {
            log_->error("Failed to bind {} to port {}!", ServerTypeToString(config.type), config.port);
            continue;
        }

        // Start STUN binding request if enabled
        if (!config.stun_server_host.empty()) {
            if (server.request_stun_binding(config.stun_server_host.c_str(), enable_ipv6_, config.stun_server_port)) {
                log_->info("[STUN:{}] Starting NAT punch via STUN server: {}:{}", config.port, config.stun_server_host, config.stun_server_port);
                stun_keepalive(server, config.port);
            } else {
                log_->error("[STUN:{}] Failed to start NAT punch via STUN server", config.port);
            }
        }

        // Add server to sock selector
#ifndef LUXON_SERVER_POLL
        if (!sock_selector_.add_read_fd(server.native_handle(), [&server](int fd) {
                ZoneScopedN("service_server");
                server.service_self();
            }))
            log_->error("Failed to add new server to sock selector!", ServerTypeToString(config.type), config.port);
#endif
    }

    next_server_it_ = servers_.end();
}
} // namespace server

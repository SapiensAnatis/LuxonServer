// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "json_fwd.hpp"
#include "logger.hpp"

#include <vector>
#include <string>
#include <memory>
#include <functional>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace server {
class ServerManager;
class App;
struct Lobby;

class HttpServer {
public:
#ifdef _WIN32
    using socket_t = SOCKET;
#else
    using socket_t = int;
#endif

    std::function<void(socket_t)> on_create_fd;
    std::function<void(socket_t)> on_delete_fd;

    explicit HttpServer(ServerManager& manager);
    ~HttpServer();

    // Bind to port and start listening (non-blocking)
    bool bind(const std::string& address, uint16_t port);

    void service_later(int fd);
    void service_now();

private:
    struct HttpClient {
        socket_t fd = -1;
        std::string request_buffer;
        std::string write_buffer;
        bool mark_for_delete = false;
        bool close_after_write = false;
        bool shutdown_sent = false;
    };

    ServerManager& server_manager_;
    std::shared_ptr<logger> log_;
    socket_t server_fd_ = -1;
    std::vector<HttpClient> clients_;

    std::vector<int> servicable_fds_;

    std::string index_html;

    void queue_data(HttpClient& client, std::string_view data, bool close_connection);

    // API handling
    void handle_client_data(HttpClient& client);
    void send_text_response(HttpClient& client, int status, std::string_view body, std::string_view content_type);
    void send_json_response(HttpClient& client, int status, const nlohmann::json& body);
    void send_error(HttpClient& client, int status, std::string_view message);

#ifdef LUXON_ENET_ENABLE_METRICS
    // Prometheus generator
    std::string generate_prometheus_metrics();

#endif
    // Routing
    nlohmann::json route_request(std::string_view method, std::string path);

    // Data helpers
    std::shared_ptr<App> find_app_by_id(std::string_view app_id);
    Lobby *find_lobby(App *app, std::string_view name, uint8_t type);
    Lobby *find_lobby(App *app, std::string_view name);
};
} // namespace server

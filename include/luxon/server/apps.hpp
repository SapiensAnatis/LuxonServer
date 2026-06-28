// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "lobby.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

namespace server {
class ServerManager;
struct Lobby;

using LobbyId = std::pair<std::string_view, uint8_t>;

struct LobbyIdHash {
    size_t operator()(const LobbyId& k) const noexcept;
};

struct AppInfo {
    std::string_view id, version;

    void encode_app_info(ser::ParameterList& params) const;
    bool has_app_info() const { return !id.empty(); }
};

struct LobbyInfo {
    AppInfo app;

    std::string_view name;
    uint8_t type{};

    operator LobbyId() const { return {name, type}; }

    void encode_lobby_info(ser::ParameterList& params) const;
    bool has_lobby_info() const { return app.has_app_info(); }
};

struct AppSettings {
    // Defaults are most relaxed for maximum compatibility

    enum AuthMode : unsigned { Weak, Anonymous, Strict };
    enum CustomAnonymousUIDMode : unsigned { Allow, AllowWithPrefix, ForceRandom };

    std::string appid;
    unsigned auth_mode = Weak;
    bool allow_find_friends = true;
    unsigned custom_anonymous_uid_mode = Allow;
    std::string anonymous_uid_prefix;
    unsigned max_peers = 0, max_peers_per_game = 0, max_game_count = 0;

    void enforce_global_config(ServerManager&);
};

class App {
    App(ServerManager& server_manager, std::string_view id, std::string_view version);

    AppSettings settings_;

    std::unordered_map<LobbyId, std::weak_ptr<Lobby>, LobbyIdHash> lobbies_;

public:
    ServerManager& server_manager;
    const std::string_view id, version;

    bool load_app_settings();

    size_t get_game_count() const;
    size_t get_peer_count() const;

    void add_app_info(ser::ParameterList& params) const;
    AppInfo get_app_info() const;

    const AppSettings& get_settings() const { return settings_; }
    std::shared_ptr<Lobby> get_lobby(LobbyId id = {});
    const std::unordered_map<LobbyId, std::weak_ptr<Lobby>, LobbyIdHash>& get_lobbies() const { return lobbies_; }

    std::shared_ptr<App> get_shared() { return get(server_manager, std::string(id), std::string(version)); }

    static std::shared_ptr<App> get(ServerManager& server_manager, const std::string& id, const std::string& version);
    static std::vector<std::shared_ptr<App>> get_all(ServerManager& server_manager);
    static AppInfo decode_app_info(const ser::ParameterList& params);
};
} // namespace server

// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"

#include <string>
#include <memory>
#include <list>
#include <vector>
#include <expected>
#include <functional>
#include <unordered_map>
#include <cstdint>
#include <luxon/ser_types.hpp>

typedef struct sqlite3 sqlite3;

namespace server {
class App;
struct Lobby;
struct Game;

struct GameListUpdateHandler {
    std::function<void(const std::shared_ptr<Game>&)> game_create;
    std::function<void(const std::shared_ptr<Game>&)> game_change;
    std::function<void(Game *)> game_delete;
};

struct Lobby : std::enable_shared_from_this<Lobby> {
    Lobby(std::shared_ptr<App> app, std::string name, uint8_t type = 0);
    ~Lobby() noexcept;

    Lobby(Lobby&&) = delete;
    Lobby(const Lobby&) = delete;
    Lobby& operator=(Lobby&&) = delete;
    Lobby& operator=(const Lobby&) = delete;

    const std::shared_ptr<App> app;
    const std::string name;
    const uint8_t type;

    std::unordered_map<std::string_view, std::weak_ptr<Game>> games;
    std::list<GameListUpdateHandler> game_list_update_handlers;

    sqlite3 *sql{};

    std::expected<std::shared_ptr<Game>, ser::OperationResponseMessage> create_game(std::string id, std::string_view address, bool or_get = false);

    size_t get_peer_count() const;
    size_t get_master_peer_count() const;

    void add_lobby_info(ser::ParameterList& params);

    // Returns exceptions with user error strings!
    std::vector<std::string> query_lobbies(const std::string& sql_queries);

    static struct LobbyInfo decode_lobby_info(const ser::ParameterList& params);
};
} // namespace server

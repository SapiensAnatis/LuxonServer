// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "game.hpp"
#include "apps.hpp"

#include <string>
#include <string_view>
#include <memory>

namespace server {
class ServerManager;
struct App;
struct Game;
class IPC;

struct PeerPersistent {
    std::shared_ptr<App> app;
    std::string user_id, token;
    std::shared_ptr<Game> current_game;
};

void store_persistent_peer(ServerManager& server_manager, std::unique_ptr<PeerPersistent>&& pp);
std::unique_ptr<PeerPersistent> load_persistent_peer(ServerManager& server_manager, std::string_view token, bool refresh_token = true);
std::unique_ptr<PeerPersistent> create_persistent_peer();
void sync_persistent_peer(ServerManager& server_manager, const PeerPersistent& pp);
} // namespace server

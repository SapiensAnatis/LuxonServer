// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "peer_persistence.hpp"
#include "server_manager.hpp"
#include "string_hash.hpp"
#include "ipc_codes.hpp"
#include "apps.hpp"
#include "game.hpp"

#include <vector>
#include <random>
#include <algorithm>
#include <luxon/common_codes.hpp>
#include <tracy/Tracy.hpp>

namespace server {
namespace {
std::string create_token(size_t length = 32) {
    static std::random_device gen;
    const std::string_view charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<size_t> dist(0, charset.size() - 1);

    std::string s(length, '\0');
    std::ranges::generate(s, [&] { return charset[dist(gen)]; });
    return s;
}
} // namespace

void store_persistent_peer(ServerManager& server_manager, std::unique_ptr<PeerPersistent>&& pp) {
    ZoneScoped;

    if (!pp->app)
        return;

    // Make sure to not store peer holding an expired game
    pp->reset_owned_game_if_created();

    // Make persistent peer expire after 30 seconds
    server_manager.add_scheduled_task(30000, [&server_manager, token = string_hash(pp->token)]() {
        // If persistent peer has not been loaded back within 30 seconds, get rid of it
        std::erase_if(server_manager.peer_persistent_data, [token](const auto& v) { return string_hash(v->token) == token; });
    });
    server_manager.peer_persistent_data.emplace_back(std::move(pp));
}

std::unique_ptr<PeerPersistent> load_persistent_peer(ServerManager& server_manager, std::string_view token, bool refresh_token) {
    ZoneScoped;

    for (auto it = server_manager.peer_persistent_data.begin(); it != server_manager.peer_persistent_data.end(); ++it) {
        if (it->get()->token != token)
            continue;

        auto fres = std::move(*it);
        server_manager.peer_persistent_data.erase(it);
        if (refresh_token)
            fres->token = create_token();
        return fres;
    }

    return nullptr;
}

std::unique_ptr<PeerPersistent> create_persistent_peer() {
    ZoneScoped;

    auto fres = std::make_unique<PeerPersistent>();
    fres->token = create_token();
    return fres;
}

void reset_persistent_peer_game_ownership(ServerManager& server_manager, Game& game) {
    for (const auto& peer : server_manager.peer_persistent_data) {
        if (peer->owns(game)) {
            peer->reset_owned_game();
            return; // No more than one peer should ever own a game
        }
    }
}

void sync_persistent_peer(ServerManager& server_manager, const PeerPersistent& pp) {
#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
    luxon::ser::EventMessage msg;
    msg.event_code = IPCEventCodes::PersistentPeerStore;
    msg.parameters[DictKeyCodes::LoadBalancing::Token] = pp.token;
    msg.parameters[DictKeyCodes::LoadBalancing::UserId] = pp.user_id;
    if (pp.has_invitation())
        pp.get_invitation().encode_game_info(msg.parameters);
    else if (pp.app)
        pp.app->add_app_info(msg.parameters);
    server_manager.ipc_broadcast(msg);
#endif
}
} // namespace server

// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "lobby.hpp"
#include "lobby_vt.hpp"
#include "server_manager.hpp"
#include "apps.hpp"
#include "game.hpp"
#include "sqlite3.h"

#include <memory>
#include <regex>
#include <format>
#include <stdexcept>
#include <commoncpp/utils.hpp>
#include <luxon/common_codes.hpp>
#include <tracy/Tracy.hpp>

namespace server {
struct SQLFinalize {
    sqlite3_stmt *& s;
    ~SQLFinalize() {
        if (s) {
            sqlite3_finalize(s);
            s = nullptr;
        }
    }
};

Lobby::Lobby(std::shared_ptr<App> app, std::string name, uint8_t type) : app(std::move(app)), name(std::move(name)), type(type) {
    ZoneScoped;

    if (type == LobbyType::SqlLobby) {
        // Open database
        int status = sqlite3_open(":memory:", &sql);
        if (status != SQLITE_OK)
            throw std::runtime_error(std::format("Failed to create SQLite DB for SQL lobby: {}", sqlite3_errstr(status)));

        // Register virtual games table
        register_lobby_virtual_table(sql, this);

        // Create virtual games table in database
        char *error_message;
        status = sqlite3_exec(sql, "CREATE VIRTUAL TABLE Games USING LobbyGames;", NULL, nullptr, &error_message);
        if (status != SQLITE_OK) {
            const std::unique_ptr<char, decltype(&sqlite3_free)> unique_error_message(error_message, sqlite3_free);
            throw std::runtime_error(std::format("Failed to initialize Virtual Table for SQL lobby: {}", error_message));
        }

        // Make database fully read-only (can only be undone using `PRAGMA query_only = OFF`)
        status = sqlite3_exec(sql, "PRAGMA query_only = ON;", NULL, nullptr, &error_message);
        if (status != SQLITE_OK) {
            const std::unique_ptr<char, decltype(&sqlite3_free)> unique_error_message(error_message, sqlite3_free);
            throw std::runtime_error(std::format("Failed to set database to read-only: {}", error_message));
        }
    }
}

Lobby::~Lobby() noexcept { sqlite3_close_v2(sql); }

std::expected<std::shared_ptr<Game>, ser::OperationResponseMessage> Lobby::create_game(std::string id, std::string_view address, bool or_get) {
    ZoneScoped;

    if (id.empty())
        return std::unexpected(ser::OperationResponseMessage{.operation_code = OpCodes::Matchmaking::CreateGame,
                                                             .return_code = ErrorCodes::Matchmaking::GameIdNotExists,
                                                             .debug_message = "Game id can't be empty"});

    if (auto res = games.find(id); res != games.end()) {
        if (or_get)
            return res->second.lock();

        return std::unexpected(ser::OperationResponseMessage{.operation_code = OpCodes::Matchmaking::CreateGame,
                                                             .return_code = ErrorCodes::Matchmaking::GameIdAlreadyExists,
                                                             .debug_message = "Game id already exists"});
    }

    const auto max_game_count = app->get_settings().max_game_count;
    if (max_game_count && app->get_game_count() > max_game_count) {
        return std::unexpected(ser::OperationResponseMessage{
            .operation_code = OpCodes::Matchmaking::CreateGame, .return_code = ErrorCodes::Server::ServerFull, .debug_message = "Game count limit reached!"});
    }

    std::shared_ptr<Game> fres(new Game(shared_from_this(), std::move(id), address), [](Game *ptr) {
        auto& lobby = ptr->lobby;

        for (auto& handler : lobby->game_list_update_handlers)
            handler.game_delete(ptr);

        auto& games = lobby->games;
        if (auto res = games.find(ptr->id); res != games.end())
            games.erase(res);

        delete ptr;
    });
    games.emplace(fres->id, fres);

    for (auto& handler : game_list_update_handlers)
        handler.game_create(fres);

    return fres;
}

size_t Lobby::get_peer_count() const {
    ZoneScoped;

    size_t fres = 0;
    for (auto& [name, weak_game] : games)
        if (auto game = weak_game.lock())
            fres += game->peers.size();
    return fres;
}

size_t Lobby::get_master_peer_count() const {
    ZoneScoped;

    size_t fres = 0;
    for (auto& [name, weak_game] : games)
        if (auto game = weak_game.lock())
            fres += !!game->find_peer(game->master_actor);
    return fres;
}

void Lobby::add_lobby_info(ser::ParameterList& params) {
    params[DictKeyCodes::AuthAndLobby::LobbyName] = name;
    params[DictKeyCodes::AuthAndLobby::LobbyType] = type;
    app->add_app_info(params);
}

std::vector<std::string> Lobby::query_lobbies(const std::string& sql_queries) {
    ZoneScoped;

    // Split into list of queries
    std::vector<std::string_view> queries = common::utils::str_split(sql_queries, ';', 3);
    if (queries.empty())
        return {};
    if (queries.back().empty())
        queries.pop_back();
    if (queries.empty())
        return {};

    // Make sure there are no more than 3 queries
    if (queries.size() > 3)
        throw std::runtime_error("There may be no more than 3 SQL queries at a time");

    // Check for excluded SQL keywords
    static const std::regex forbidden_keywords(R"(\b(ALTER|CREATE|DELETE|DROP|EXEC|EXECUTE|INSERT|MERGE|SELECT|UPDATE|UNION)\b)",
                                               std::regex::icase | std::regex::optimize);

    if (std::regex_search(sql_queries, forbidden_keywords))
        throw std::runtime_error("SQL filter contains excluded keywords.");

    std::vector<std::string> results;

    // Process chained filters in order
    for (const auto& filter : queries) {
        if (filter.empty())
            continue;

        // Select the __id using the user's WHERE condition
        std::string full_query = std::format("SELECT __id FROM Games WHERE {} LIMIT 100;", filter);

        sqlite3_stmt *stmt = nullptr;
        SQLFinalize stmt_guard{stmt};

        int status = sqlite3_prepare_v2(sql, full_query.c_str(), -1, &stmt, nullptr);
        if (status != SQLITE_OK)
            throw std::runtime_error(std::format("SQL preparation failed: {}", sqlite3_errstr(status)));

        // Fetch matching room IDs
        while ((status = sqlite3_step(stmt)) == SQLITE_ROW) {
            const unsigned char *id_text = sqlite3_column_text(stmt, 0);
            if (id_text)
                results.emplace_back(reinterpret_cast<const char *>(id_text));
        }

        if (status != SQLITE_DONE)
            throw std::runtime_error(std::format("SQL execution failed: {}", sqlite3_errstr(status)));

        // If at least one match in this chain link was found, stop and return the results
        if (!results.empty())
            break;
    }

    return results;
}

LobbyInfo Lobby::decode_lobby_info(const ser::ParameterList& params) {
    LobbyInfo fres(App::decode_app_info(params));
    for (const auto& [key, val] : params) {
        if (key == DictKeyCodes::AuthAndLobby::LobbyName)
            fres.lobby_name = val.get<std::string>();
        if (key == DictKeyCodes::AuthAndLobby::LobbyType)
            fres.lobby_type = val.get<uint8_t>();
    }
    return fres;
}
} // namespace server

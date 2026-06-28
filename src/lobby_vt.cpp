// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "lobby_vt.hpp"
#include "lobby.hpp"
#include "game.hpp"
#include "sqlite3.h"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <format>
#include <stdexcept>
#include <luxon/ser_types.hpp>
#include <tracy/Tracy.hpp>

namespace server {
static const luxon::ser::Value& get_prop_key(int index) {
    static const luxon::ser::Value keys[10] = {
        luxon::ser::Value("C0"), luxon::ser::Value("C1"), luxon::ser::Value("C2"), luxon::ser::Value("C3"), luxon::ser::Value("C4"),
        luxon::ser::Value("C5"), luxon::ser::Value("C6"), luxon::ser::Value("C7"), luxon::ser::Value("C8"), luxon::ser::Value("C9"),
    };

    if (index < 0 || index >= 10)
        throw std::out_of_range("get_prop_key: index out of range");
    return keys[index];
}

struct LobbyVTab {
    sqlite3_vtab base{};
    Lobby *lobby = nullptr;
};

struct LobbyCursor {
    sqlite3_vtab_cursor base{};
    std::vector<std::shared_ptr<Game>> games_snapshot;
    size_t current_index = 0;
};

static void set_vtab_error(sqlite3_vtab *pVTab, const std::exception& e) {
    if (!pVTab)
        return;
    if (pVTab->zErrMsg)
        sqlite3_free(pVTab->zErrMsg);
    pVTab->zErrMsg = sqlite3_mprintf("C++ Exception: %s", e.what());
}

static int vtConnect(sqlite3 *db, void *pAux, int /*argc*/, const char *const * /*argv*/, sqlite3_vtab **ppVTab, char **pzErr) {
    ZoneScoped;

    try {
        if (ppVTab)
            *ppVTab = nullptr;

        int rc = sqlite3_declare_vtab(db, "CREATE TABLE x("
                                          "__id TEXT, "
                                          "C0 COLLATE NOCASE, C1 COLLATE NOCASE, C2 COLLATE NOCASE, C3 COLLATE NOCASE, C4 COLLATE NOCASE, "
                                          "C5 COLLATE NOCASE, C6 COLLATE NOCASE, C7 COLLATE NOCASE, C8 COLLATE NOCASE, C9 COLLATE NOCASE"
                                          ");");
        if (rc != SQLITE_OK) {
            if (pzErr)
                *pzErr = sqlite3_mprintf("%s", sqlite3_errmsg(db));
            return rc;
        }

        auto *v = new LobbyVTab{};
        v->lobby = static_cast<Lobby *>(pAux);

        *ppVTab = &v->base;
        return SQLITE_OK;
    } catch (const std::exception& e) {
        if (pzErr)
            *pzErr = sqlite3_mprintf("C++ Exception: %s", e.what());
        return SQLITE_ERROR;
    } catch (...) {
        if (pzErr)
            *pzErr = sqlite3_mprintf("Unknown C++ exception in vtConnect");
        return SQLITE_ERROR;
    }
}

static int vtDisconnect(sqlite3_vtab *pVTab) {
    ZoneScoped;

    try {
        auto *v = reinterpret_cast<LobbyVTab *>(pVTab);
        delete v;
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int vtBestIndex(sqlite3_vtab *pVTab, sqlite3_index_info *pIdxInfo) {
    ZoneScoped;

    try {
        int argvIndex = 1;
        int idxNum = 0;
        std::string idxStr;

        // Columns:
        //  0  -> __id
        //  1  -> C0
        //  ...
        // 10  -> C9
        for (int i = 0; i < pIdxInfo->nConstraint; i++) {
            const auto& constraint = pIdxInfo->aConstraint[i];
            if (!constraint.usable)
                continue;

            const int col = constraint.iColumn;

            char opChar = 0;
            if (constraint.op == SQLITE_INDEX_CONSTRAINT_EQ)
                opChar = '=';
            else if (constraint.op == SQLITE_INDEX_CONSTRAINT_LIKE)
                opChar = 'L';
            else if (constraint.op == SQLITE_INDEX_CONSTRAINT_GLOB)
                opChar = 'G';
            else
                continue;

            if (col < 0 || col > 10)
                continue;

            pIdxInfo->aConstraintUsage[i].argvIndex = argvIndex++;
            pIdxInfo->aConstraintUsage[i].omit = 1;

            idxStr += std::to_string(col);
            idxStr += opChar;
            idxStr += ';';

            if (col == 0 && opChar == '=')
                idxNum |= 1;
        }

        if (!idxStr.empty()) {
            pIdxInfo->idxStr = sqlite3_mprintf("%s", idxStr.c_str());
            pIdxInfo->needToFreeIdxStr = 1;
        }

        pIdxInfo->idxNum = idxNum;
        pIdxInfo->estimatedCost = (idxNum & 1) ? 1.0 : 1000.0;

        return SQLITE_OK;
    } catch (const std::exception& e) {
        set_vtab_error(pVTab, e);
        return SQLITE_ERROR;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int vtOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor) {
    ZoneScoped;

    try {
        if (ppCursor)
            *ppCursor = nullptr;

        auto *c = new LobbyCursor{};
        c->base.pVtab = pVTab;
        c->current_index = 0;

        *ppCursor = &c->base;
        return SQLITE_OK;
    } catch (const std::exception& e) {
        set_vtab_error(pVTab, e);
        return SQLITE_NOMEM;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int vtClose(sqlite3_vtab_cursor *cur) {
    ZoneScoped;

    try {
        auto *c = reinterpret_cast<LobbyCursor *>(cur);
        delete c;
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int vtFilter(sqlite3_vtab_cursor *cur, int idxNum, const char *idxStr, int argc, sqlite3_value **argv) {
    ZoneScoped;

    auto *pCur = reinterpret_cast<LobbyCursor *>(cur);
    auto *pVTab = reinterpret_cast<LobbyVTab *>(cur->pVtab);

    try {
        pCur->games_snapshot.clear();
        pCur->current_index = 0;

        struct FilterProp {
            int col = 0;
            char op = 0;
            std::string val;
            bool is_null = false;
        };

        std::vector<FilterProp> filter_props;
        std::string filter_id;
        bool filter_id_is_null = false;

        // Parse planning instructions from idxStr (generated by vtBestIndex)
        if (idxStr != nullptr) {
            std::string s(idxStr);
            size_t start = 0;
            int arg_idx = 0;

            while (start < s.size()) {
                size_t end = s.find(';', start);
                if (end == std::string::npos)
                    break;

                std::string token = s.substr(start, end - start);
                start = end + 1;

                if (token.empty())
                    continue;
                if (arg_idx >= argc) {
                    throw std::runtime_error("vtFilter: constraint/argv mismatch");
                }

                const char op = token.back();
                const std::string col_str = token.substr(0, token.size() - 1);
                const int col = std::stoi(col_str);

                const bool is_null = (sqlite3_value_type(argv[arg_idx]) == SQLITE_NULL);
                const unsigned char *text_val = sqlite3_value_text(argv[arg_idx]);
                ++arg_idx;

                std::string val_str = text_val ? reinterpret_cast<const char *>(text_val) : "";

                if (col == 0 && op == '=') {
                    filter_id = std::move(val_str);
                    filter_id_is_null = is_null;
                } else {
                    filter_props.push_back(FilterProp{col, op, std::move(val_str), is_null});
                }
            }
        }

        // `__id = NULL` matches no rows
        if ((idxNum & 1) && filter_id_is_null)
            return SQLITE_OK;

        // Build matcher
        auto matches_props = [&](const std::shared_ptr<Game>& game) -> bool {
            for (const auto& prop : filter_props) {
                if (prop.is_null)
                    return false;

                std::string actual_str;

                if (prop.col == 0) {
                    actual_str = game->id;
                } else if (prop.col >= 1 && prop.col <= 10) {
                    auto it = game->custom_props.find(get_prop_key(prop.col - 1));
                    if (it == game->custom_props.end())
                        return false;

                    const auto& val = it->second;
                    if (val.is<std::string>())
                        actual_str = val.get<std::string>();
                    else if (val.is<bool>())
                        actual_str = val.get<bool>() ? "1" : "0";
                    else if (val.is<int32_t>())
                        actual_str = std::to_string(val.get<int32_t>());
                    else if (val.is<int64_t>())
                        actual_str = std::to_string(val.get<int64_t>());
                    else if (val.is<int16_t>())
                        actual_str = std::to_string(val.get<int16_t>());
                    else if (val.is<uint8_t>())
                        actual_str = std::to_string(val.get<uint8_t>());
                    else
                        return false;
                } else {
                    // Out-of-range column request
                    return false;
                }

                if (prop.op == '=') {
                    if (sqlite3_stricmp(actual_str.c_str(), prop.val.c_str()) != 0)
                        return false;
                } else if (prop.op == 'L') {
                    if (sqlite3_strlike(prop.val.c_str(), actual_str.c_str(), 0) != 0)
                        return false;
                } else if (prop.op == 'G') {
                    if (sqlite3_strglob(prop.val.c_str(), actual_str.c_str()) != 0)
                        return false;
                } else {
                    // Unknown op
                    return false;
                }
            }
            return true;
        };

        // Execute search
        if (idxNum & 1) {
            auto it = pVTab->lobby->games.find(filter_id);
            if (it != pVTab->lobby->games.end()) {
                if (auto game = it->second.lock()) {
                    if (matches_props(game))
                        pCur->games_snapshot.push_back(std::move(game));
                }
            }
        } else {
            for (const auto& [id, weak_game] : pVTab->lobby->games) {
                (void)id;
                if (auto game = weak_game.lock()) {
                    if (matches_props(game))
                        pCur->games_snapshot.push_back(std::move(game));
                }
            }
        }

        return SQLITE_OK;
    } catch (const std::exception& e) {
        set_vtab_error(&pVTab->base, e);
        return SQLITE_ERROR;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int vtNext(sqlite3_vtab_cursor *cur) {
    reinterpret_cast<LobbyCursor *>(cur)->current_index++;
    return SQLITE_OK;
}

static int vtEof(sqlite3_vtab_cursor *cur) {
    auto *pCur = reinterpret_cast<LobbyCursor *>(cur);
    return (pCur->current_index >= pCur->games_snapshot.size()) ? 1 : 0;
}

static int vtColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int i) {
    ZoneScoped;

    try {
        auto *pCur = reinterpret_cast<LobbyCursor *>(cur);
        if (pCur->current_index >= pCur->games_snapshot.size()) {
            sqlite3_result_null(ctx);
            return SQLITE_OK;
        }

        const auto& game = pCur->games_snapshot[pCur->current_index];

        if (i == 0) {
            sqlite3_result_text(ctx, game->id.c_str(), -1, SQLITE_TRANSIENT);
            return SQLITE_OK;
        }

        if (i >= 1 && i <= 10) {
            auto it = game->custom_props.find(get_prop_key(i - 1));
            if (it != game->custom_props.end()) {
                const auto& val = it->second;

                if (val.is<std::string>()) {
                    const auto& s = val.get<std::string>();
                    sqlite3_result_text(ctx, s.c_str(), -1, SQLITE_TRANSIENT);
                    return SQLITE_OK;
                }
                if (val.is<bool>()) {
                    sqlite3_result_int(ctx, val.get<bool>());
                    return SQLITE_OK;
                }
                if (val.is<int32_t>()) {
                    sqlite3_result_int(ctx, val.get<int32_t>());
                    return SQLITE_OK;
                }
                if (val.is<int64_t>()) {
                    sqlite3_result_int64(ctx, val.get<int64_t>());
                    return SQLITE_OK;
                }
                if (val.is<int16_t>()) {
                    sqlite3_result_int(ctx, val.get<int16_t>());
                    return SQLITE_OK;
                }
                if (val.is<uint8_t>()) {
                    sqlite3_result_int(ctx, val.get<uint8_t>());
                    return SQLITE_OK;
                }

                sqlite3_result_null(ctx);
                return SQLITE_OK;
            }
        }

        sqlite3_result_null(ctx);
        return SQLITE_OK;
    } catch (...) {
        sqlite3_result_error(ctx, "C++ exception in vtColumn", -1);
        return SQLITE_ERROR;
    }
}

static int vtRowid(sqlite3_vtab_cursor *cur, sqlite3_int64 *pRowid) {
    ZoneScoped;

    try {
        auto *pCur = reinterpret_cast<LobbyCursor *>(cur);

        // Rowid must be unique within the result set, using the cursor index avoids hash collisions
        const size_t idx = pCur->current_index;
        if (idx < pCur->games_snapshot.size()) {
            // Avoid returning 0; SQLite rowid can be 0 but it's better to stay above
            *pRowid = static_cast<sqlite3_int64>(idx + 1);
        } else {
            *pRowid = 0;
        }
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static sqlite3_module lobbyGamesModule = {.iVersion = 1,
                                          .xCreate = vtConnect,
                                          .xConnect = vtConnect,
                                          .xBestIndex = vtBestIndex,
                                          .xDisconnect = vtDisconnect,
                                          .xDestroy = vtDisconnect,
                                          .xOpen = vtOpen,
                                          .xClose = vtClose,
                                          .xFilter = vtFilter,
                                          .xNext = vtNext,
                                          .xEof = vtEof,
                                          .xColumn = vtColumn,
                                          .xRowid = vtRowid,
                                          .xUpdate = nullptr,
                                          .xBegin = nullptr,
                                          .xSync = nullptr,
                                          .xCommit = nullptr,
                                          .xRollback = nullptr,
                                          .xFindFunction = nullptr,
                                          .xRename = nullptr,
                                          .xSavepoint = nullptr,
                                          .xRelease = nullptr,
                                          .xRollbackTo = nullptr,
                                          .xShadowName = nullptr,
                                          .xIntegrity = nullptr};

void register_lobby_virtual_table(sqlite3 *db, Lobby *lobby) {
    const int rc = sqlite3_create_module(db, "LobbyGames", &lobbyGamesModule, lobby);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::format("Failed to register LobbyGames virtual table module: {}", sqlite3_errmsg(db)));
    }
}
} // namespace server

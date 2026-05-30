// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FFI_HANDLE_TYPE
#if defined(FFI_WASM) || defined(__wasm__)
#define FFI_HANDLE_TYPE(name) typedef uint32_t name;
#else
#define FFI_HANDLE_TYPE(name) typedef struct name##_opaque *name;
#endif
#endif

#ifndef FFI_FUNCTION
#define FFI_FUNCTION(ret, name, params, ...) FFI_EXPORT(#name) ret name params;
#endif

/* ============================================================================
 * HANDLE TYPES TABLE
 * ============================================================================ */

FFI_HANDLE_TYPE(ServerManagerHandle)
FFI_HANDLE_TYPE(ServerManagerConfigHandle)
FFI_HANDLE_TYPE(GameHandle)
FFI_HANDLE_TYPE(LobbyHandle)
FFI_HANDLE_TYPE(PeerHandle)
FFI_HANDLE_TYPE(GamePeerHandle)
FFI_HANDLE_TYPE(EventHandle)
FFI_HANDLE_TYPE(LoggerHandle)
FFI_HANDLE_TYPE(ByteArrayHandle)
FFI_HANDLE_TYPE(SerValueHandle)
FFI_HANDLE_TYPE(SerMessageHandle)
FFI_HANDLE_TYPE(HandlerBaseHandle)
FFI_HANDLE_TYPE(CommandRestarterHandle)

/* ============================================================================
 * FUNCTIONS TABLE
 * ============================================================================ */

/* --- EXCEPTIONS INTERFACE --- */
FFI_FUNCTION(bool, luxonHasError, (), )
FFI_FUNCTION(const char *, luxonGetLastErrorType, (), )
FFI_FUNCTION(const char *, luxonGetLastErrorMessage, (), )
FFI_FUNCTION(void, luxonClearLastError, (), )

/* --- BYTE ARRAY INTERFACE --- */
FFI_FUNCTION(ByteArrayHandle, createByteArray, (), )
FFI_FUNCTION(void, destroyByteArray, (ByteArrayHandle val), val)
FFI_FUNCTION(ffi_size_t, copyFromByteArray, (ByteArrayHandle val, uint8_t *out_buf, ffi_size_t max_len), val, out_buf, max_len)
FFI_FUNCTION(void, appendToByteArray, (ByteArrayHandle val, const uint8_t *buf, ffi_size_t len), buf, len)
FFI_FUNCTION(void, copyToByteArray, (ByteArrayHandle val, const uint8_t *buf, ffi_size_t len), buf, len)
FFI_FUNCTION(ffi_size_t, getByteArraySize, (ByteArrayHandle val))
FFI_FUNCTION(void *, getByteArrayDataPtr, (ByteArrayHandle val))
FFI_FUNCTION(void, pushToByteArray, (ByteArrayHandle val, uint8_t byte), val, byte)
FFI_FUNCTION(uint8_t, getInByteArray, (ByteArrayHandle val, ffi_size_t index), val, index)
FFI_FUNCTION(void, setInByteArray, (ByteArrayHandle val, ffi_size_t index, uint8_t byte), val, index, byte)

/* --- SER MESSAGE INTERFACE --- */
FFI_FUNCTION(SerMessageHandle, createSerMessage, (), )
FFI_FUNCTION(void, destroySerMessage, (SerMessageHandle val), val)
FFI_FUNCTION(ByteArrayHandle, serializeSerMessage, (SerMessageHandle val), val)
FFI_FUNCTION(bool, deserializeSerMessage, (const uint8_t *buf, ffi_size_t len, SerMessageHandle out_val), buf, len, out_val)
FFI_FUNCTION(bool, deserializeSerMessageFromByteArray, (ByteArrayHandle val, SerMessageHandle out_val), val, out_val)

/* --- SER VALUE INTERFACE --- */
FFI_FUNCTION(SerValueHandle, createSerValue, (), )
FFI_FUNCTION(void, destroySerValue, (SerValueHandle val), val)
FFI_FUNCTION(ByteArrayHandle, serializeSerValue, (SerValueHandle val), val)
FFI_FUNCTION(bool, deserializeSerValue, (const uint8_t *buf, ffi_size_t len, SerValueHandle out_val), buf, len, out_val)
FFI_FUNCTION(bool, deserializeSerValueFromByteArray, (ByteArrayHandle val, SerValueHandle out_val), val, out_val)

/* --- LOGGER INTERFACE --- */
#ifndef LUXON_SERVER_USE_SPDLOG
FFI_FUNCTION(void, luxonEnableCustomLogSink, (bool enable), enable)
#endif
FFI_FUNCTION(LoggerHandle, getOrCreateLogger, (const char *name), name)
FFI_FUNCTION(void, setLoggerLevel, (LoggerHandle logger, int32_t level), logger, level)
FFI_FUNCTION(void, loggerTrace, (LoggerHandle logger, const char *message), logger, message)
FFI_FUNCTION(void, loggerDebug, (LoggerHandle logger, const char *message), logger, message)
FFI_FUNCTION(void, loggerInfo, (LoggerHandle logger, const char *message), logger, message)
FFI_FUNCTION(void, loggerWarn, (LoggerHandle logger, const char *message), logger, message)
FFI_FUNCTION(void, loggerError, (LoggerHandle logger, const char *message), logger, message)
FFI_FUNCTION(void, loggerCritical, (LoggerHandle logger, const char *message), logger, message)

/* --- PEER INTERFACE --- */
FFI_FUNCTION(bool, peerIsAuthenticated, (PeerHandle peer), peer)
FFI_FUNCTION(LuxonServerProtocol, peerGetTransportProtocol, (PeerHandle peer), peer)
FFI_FUNCTION(void, peerSend, (PeerHandle peer, const uint8_t *payload, ffi_size_t length, uint8_t channel, LuxonDeliveryMode delivery_mode), peer, payload,
             length, channel, delivery_mode)
FFI_FUNCTION(void, peerDisconnect, (PeerHandle peer), peer)
FFI_FUNCTION(const char *, peerGetUserId, (PeerHandle peer), peer)
FFI_FUNCTION(GameHandle, peerGetCurrentGame, (PeerHandle peer), peer)

/* --- GAME PEER INTERFACE --- */
FFI_FUNCTION(GamePeerHandle, createGamePeerContainer, (), )
FFI_FUNCTION(void, destroyGamePeerContainer, (GamePeerHandle game_peer), game_peer)
FFI_FUNCTION(bool, gamePeerIsValid, (GamePeerHandle game_peer), game_peer)
FFI_FUNCTION(int32_t, gamePeerGetActorId, (GamePeerHandle game_peer), game_peer)
FFI_FUNCTION(bool, gamePeerHasInterestGroup, (GamePeerHandle game_peer, uint8_t group), game_peer, group)
FFI_FUNCTION(void, gamePeerSetInterestGroup, (GamePeerHandle game_peer, uint8_t group, bool enable), game_peer, group, enable)
FFI_FUNCTION(void, gamePeerGetInterestGroupsMask, (GamePeerHandle game_peer, uint32_t *out_mask_words, ffi_size_t max_words), game_peer, out_mask_words,
             max_words)
FFI_FUNCTION(void, gamePeerSetInterestGroupsMask, (GamePeerHandle game_peer, const uint32_t *mask_words, ffi_size_t word_count), game_peer, mask_words,
             word_count)
FFI_FUNCTION(void, gamePeerGetActorProps, (GamePeerHandle game_peer, SerValueHandle out_props), game_peer, out_props)
FFI_FUNCTION(void, gamePeerSetActorProps, (GamePeerHandle game_peer, SerValueHandle props), game_peer, props)
FFI_FUNCTION(PeerHandle, gamePeerGetBasePeer, (GamePeerHandle game_peer), game_peer)
FFI_FUNCTION(bool, gamePeerDisconnect, (GamePeerHandle game_peer), game_peer)

/* --- EVENT INTERFACE --- */
FFI_FUNCTION(EventHandle, createEvent, (), )
FFI_FUNCTION(void, destroyEvent, (EventHandle event), event)
FFI_FUNCTION(void, eventSetRoutingMetadata,
             (EventHandle event, uint8_t code, int32_t sender_actor_id, LuxonDeliveryMode delivery_mode, uint8_t channel, uint8_t interest_group), event, code,
             sender_actor_id, delivery_mode, channel, interest_group)
FFI_FUNCTION(void, eventGetRoutingMetadata,
             (EventHandle event, uint8_t *out_code, int32_t *out_sender_id, LuxonDeliveryMode *out_mode, uint8_t *out_channel, uint8_t *out_group), event,
             out_code, out_sender_id, out_mode, out_channel, out_group)
FFI_FUNCTION(void, eventSetReceiversAll, (EventHandle event), event)
FFI_FUNCTION(void, eventSetReceiversGroup, (EventHandle event, uint8_t group), event, group)
FFI_FUNCTION(void, eventSetReceiversActors, (EventHandle event, const int32_t *actor_ids, ffi_size_t count), event, actor_ids, count)
FFI_FUNCTION(LuxonEventReceiversType, eventGetReceiversType, (EventHandle event), event)
FFI_FUNCTION(uint8_t, eventGetReceiversGroup, (EventHandle event), event)
FFI_FUNCTION(void, eventGetReceiversActors, (EventHandle event, int32_t *out_actor_ids, ffi_size_t max_count, ffi_size_t *out_written), event, out_actor_ids,
             max_count, out_written)
FFI_FUNCTION(void, eventSetData, (EventHandle event, SerValueHandle data), event, data)
FFI_FUNCTION(void, eventGetData, (EventHandle event, SerValueHandle out_data), event, out_data)
FFI_FUNCTION(void, eventSetTopParams, (EventHandle event, SerValueHandle top_params), event, top_params)
FFI_FUNCTION(void, eventGetTopParams, (EventHandle event, SerValueHandle out_top_params), event, out_top_params)

/* --- GAME INTERFACE --- */
FFI_FUNCTION(void, gameGetId, (GameHandle game, char *out_id, ffi_size_t max_len), game, out_id, max_len)
FFI_FUNCTION(void, gameGetConfigState,
             (GameHandle game, uint8_t *out_flags, bool *out_is_created, bool *out_is_open, bool *out_is_visible, uint8_t *out_max_peers,
              int32_t *out_master_actor),
             game, out_flags, out_is_created, out_is_open, out_is_visible, out_max_peers, out_master_actor)
FFI_FUNCTION(void, gameSetConfigState, (GameHandle game, uint8_t flags, bool is_open, bool is_visible, uint8_t max_peers, int32_t master_actor), game, flags,
             is_open, is_visible, max_peers, master_actor)
FFI_FUNCTION(int32_t, gameGetMasterActor, (GameHandle game), game)
FFI_FUNCTION(void, gameSetMasterActor, (GameHandle game, int32_t actor_id), game, actor_id)
FFI_FUNCTION(int32_t, gameGetLastActorId, (GameHandle game), game)
FFI_FUNCTION(uint8_t, gameGetMaxPeers, (GameHandle game), game)
FFI_FUNCTION(void, gameSetMaxPeers, (GameHandle game, uint8_t max_peers), game, max_peers)
FFI_FUNCTION(void, gameGetTtlConfig, (GameHandle game, int32_t *out_player_ttl, int32_t *out_empty_ttl), game, out_player_ttl, out_empty_ttl)
FFI_FUNCTION(void, gameSetTtlConfig, (GameHandle game, int32_t player_ttl, int32_t empty_ttl), game, player_ttl, empty_ttl)
FFI_FUNCTION(void, gameGetCustomProps, (GameHandle game, SerValueHandle out_custom_props), game, out_custom_props)
FFI_FUNCTION(void, gameSetCustomProps, (GameHandle game, SerValueHandle custom_props), game, custom_props)
FFI_FUNCTION(void, gameGetLobbyPropsToSerValue, (GameHandle game, SerValueHandle out_keys_array), game, out_keys_array)
FFI_FUNCTION(void, gameSetLobbyPropsFromSerValue, (GameHandle game, SerValueHandle keys_array), game, keys_array)
FFI_FUNCTION(void, gameGetExpectedUsersToSerValue, (GameHandle game, SerValueHandle out_list), game, out_list)
FFI_FUNCTION(void, gameSetExpectedUsersFromSerValue, (GameHandle game, SerValueHandle list), game, list)
FFI_FUNCTION(void, gameAddExpectedUser, (GameHandle game, const char *user_id), game, user_id)
FFI_FUNCTION(void, gameRemoveExpectedUser, (GameHandle game, const char *user_id), game, user_id)
FFI_FUNCTION(ffi_size_t, gameGetPeerCount, (GameHandle game), game)
FFI_FUNCTION(GamePeerHandle, gameCreatePeer, (GameHandle game, PeerHandle peer), game, peer)
FFI_FUNCTION(GamePeerHandle, gameAddPeer, (GameHandle game, GamePeerHandle game_peer), game, game_peer)
FFI_FUNCTION(bool, gameRemovePeer, (GameHandle game, PeerHandle peer), game, peer)
FFI_FUNCTION(bool, gameFloodPeer, (GameHandle game, GamePeerHandle game_peer), game, game_peer)
FFI_FUNCTION(GamePeerHandle, gameFindPeerByActorId, (GameHandle game, int32_t actor_id), game, actor_id)
FFI_FUNCTION(GamePeerHandle, gameFindPeerByBasePeer, (GameHandle game, PeerHandle peer), game, peer)
FFI_FUNCTION(void, gameBroadcastEvent, (GameHandle game, EventHandle event), game, event)
FFI_FUNCTION(bool, gameValidateJoin,
             (GameHandle game, const char *user_id, ffi_size_t new_expected_users_count, int16_t *out_err_code, char *out_err_str, ffi_size_t max_err_len),
             game, user_id, new_expected_users_count, out_err_code, out_err_str, max_err_len)
FFI_FUNCTION(void, gameTriggerLobbyUpdate, (GameHandle game), game)
FFI_FUNCTION(void, gameGetGameProp, (GameHandle game, SerValueHandle key, SerValueHandle out_val), game, key, out_val)
FFI_FUNCTION(void, gameGetLobbyGameProps, (GameHandle game, SerValueHandle out_hashtable), game, out_hashtable)
FFI_FUNCTION(void, gameGetGameProps, (GameHandle game, bool no_custom, SerValueHandle out_hashtable), game, no_custom, out_hashtable)
FFI_FUNCTION(void, gameGetActorProps, (GameHandle game, SerValueHandle out_hashtable), game, out_hashtable)
FFI_FUNCTION(void, gameInsertGameProps, (GameHandle game, SerValueHandle update), game, update)
FFI_FUNCTION(bool, gameExpectGameProps, (GameHandle game, SerValueHandle expected), game, expected)
FFI_FUNCTION(bool, gameInsertActorProps, (GameHandle game, int32_t actor_id, SerValueHandle update), game, actor_id, update)
FFI_FUNCTION(bool, gameExpectActorProps, (GameHandle game, int32_t actor_id, SerValueHandle expected), game, actor_id, expected)
FFI_FUNCTION(bool, gameMatchesFilter, (SerValueHandle event_data, SerValueHandle filter), event_data, filter)

/* --- LOBBY INTERFACE --- */
FFI_FUNCTION(LobbyHandle, createLobby, (const char *name, uint8_t type), name, type)
FFI_FUNCTION(void, destroyLobby, (LobbyHandle lobby), lobby)
FFI_FUNCTION(GameHandle, lobbyCreateGame, (LobbyHandle lobby, const char *id, bool or_get, SerMessageHandle out_err_resp), lobby, id, or_get, out_err_resp)
FFI_FUNCTION(ffi_size_t, lobbyGetPeerCount, (LobbyHandle lobby), lobby)
FFI_FUNCTION(ffi_size_t, lobbyGetMasterPeerCount, (LobbyHandle lobby), lobby)
FFI_FUNCTION(bool, lobbyQueryLobbies, (LobbyHandle lobby, const char *sql_queries, char *out_buf, ffi_size_t max_len, ffi_size_t *out_written), lobby,
             sql_queries, out_buf, max_len, out_written)

/* --- SERVER MANAGER CONFIG BUILDER INTERFACE --- */
FFI_FUNCTION(ServerManagerConfigHandle, createServerManagerConfig, (), )
FFI_FUNCTION(void, destroyServerManagerConfig, (ServerManagerConfigHandle config), config)
FFI_FUNCTION(ServerManagerConfigHandle, loadServerManagerConfigFromFile, (const char *config_file_path), config_file_path)
FFI_FUNCTION(ServerManagerConfigHandle, parseServerManagerConfig, (const char *yaml_config_contents), yaml_config_contents)
FFI_FUNCTION(void, serverManagerConfigAddServer, (ServerManagerConfigHandle config, LuxonServerType type, uint16_t port), config, type, port)
FFI_FUNCTION(void, serverManagerConfigAddServerWithUdp,
             (ServerManagerConfigHandle config, LuxonServerType type, uint16_t port, const char *external_udp_address), config, type, port,
             external_udp_address)
FFI_FUNCTION(void, serverManagerConfigAddEndpoint, (ServerManagerConfigHandle config, LuxonServerType type, LuxonServerProtocol protocol, const char *address),
             config, type, protocol, address)
FFI_FUNCTION(void, serverManagerConfigSetLimits,
             (ServerManagerConfigHandle config, bool enable_ipv6, unsigned max_connections, unsigned max_game_peers, uint32_t tick_time_budget), config,
             enable_ipv6, max_connections, max_game_peers, tick_time_budget)
FFI_FUNCTION(void, serverManagerConfigSetSettingsDatabasePath, (ServerManagerConfigHandle config, const char *path), config, path)
FFI_FUNCTION(void, serverManagerConfigEnableHttp, (ServerManagerConfigHandle config, const char *address, uint16_t port), config, address, port)
FFI_FUNCTION(void, serverManagerConfigDisableHttp, (ServerManagerConfigHandle config), config)

/* --- SERVER MANAGER INTERFACE --- */
FFI_FUNCTION(ServerManagerHandle, createServerManagerFromConfig, (ServerManagerConfigHandle config), config)
FFI_FUNCTION(ServerManagerHandle, createServerManagerFromFile, (const char *config_file_path), config_file_path)
FFI_FUNCTION(ServerManagerHandle, createServerManagerFromContents, (const char *yaml_config_contents), yaml_config_contents)
FFI_FUNCTION(void, destroyServerManager, (ServerManagerHandle manager), manager)
FFI_FUNCTION(void, serverManagerRun, (ServerManagerHandle manager), manager)
FFI_FUNCTION(bool, serverManagerRunOnce, (ServerManagerHandle manager), manager)
FFI_FUNCTION(void, serverManagerStop, (ServerManagerHandle manager), manager)
FFI_FUNCTION(bool, serverManagerGetEndpointOf,
             (ServerManagerHandle manager, LuxonServerType server_type, LuxonServerProtocol server_proto, char *out_address, ffi_size_t max_len), manager,
             server_type, server_proto, out_address, max_len)
FFI_FUNCTION(ffi_size_t, serverManagerGetConnectionCount, (ServerManagerHandle manager), manager)
FFI_FUNCTION(unsigned, serverManagerGetMaxConnections, (ServerManagerHandle manager), manager)
FFI_FUNCTION(uint8_t, serverManagerGetMaxGamePeers, (ServerManagerHandle manager), manager)

/* --- HANDLER / GAME EXTENSIONS --- */
FFI_FUNCTION(ServerManagerHandle, handlerBaseGetServerManager, (HandlerBaseHandle handler), handler)
FFI_FUNCTION(ServerManagerHandle, gameGetServerManager, (GameHandle game), game)

/* --- COMMAND RESTARTER INTERFACE --- */
FFI_FUNCTION(bool, serverManagerShouldAbortActiveCommand, (ServerManagerHandle manager), manager)
FFI_FUNCTION(void, serverManagerMarkCommandCommited, (ServerManagerHandle manager), manager)
#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
FFI_FUNCTION(CommandRestarterHandle, serverManagerTakeCommandRestarter, (ServerManagerHandle manager), manager)
FFI_FUNCTION(void, commandRestarterRestart, (CommandRestarterHandle restarter), restarter)
FFI_FUNCTION(void, destroyCommandRestarter, (CommandRestarterHandle restarter), restarter)
#endif

/* --- PLUGINS / HOOKPOINTS REGISTRATION INTERFACE --- */
#ifdef LUXON_SERVER_ENABLE_PLUGINS
FFI_FUNCTION(uint32_t, registerGamePlugin, (const char *plugin_name), plugin_name)
FFI_FUNCTION(bool, registerAuthPlugin, (unsigned type), type)
#endif
#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
FFI_FUNCTION(void, registerHookpoints, (ServerManagerHandle manager), manager)
#endif

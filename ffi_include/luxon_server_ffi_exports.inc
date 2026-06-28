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

/** @brief Checks if a native error is pending on the current thread */
FFI_FUNCTION(bool, luxonHasError, (), )
/** @brief Retrieves the exception type name of the last recorded error */
FFI_FUNCTION(const char *, luxonGetLastErrorType, (), )
/** @brief Retrieves the message of the last recorded error */
FFI_FUNCTION(const char *, luxonGetLastErrorMessage, (), )
/** @brief Clears the current thread's error state */
FFI_FUNCTION(void, luxonClearLastError, (), )

/* --- BYTE ARRAY INTERFACE --- */

/** @brief Instantiates a new ByteArray */
FFI_FUNCTION(ByteArrayHandle, createByteArray, (), )
/** @brief Destroys a ByteArray, freeing its memory */
FFI_FUNCTION(void, destroyByteArray, (ByteArrayHandle val), val)
/** @brief Copies data from a ByteArray into a provided buffer */
FFI_FUNCTION(ffi_size_t, copyFromByteArray, (ByteArrayHandle val, uint8_t *out_buf, ffi_size_t max_len), val, out_buf, max_len)
/** @brief Appends data to the end of a ByteArray */
FFI_FUNCTION(void, appendToByteArray, (ByteArrayHandle val, const uint8_t *buf, ffi_size_t len), buf, len)
/** @brief Overwrites the ByteArray with new data */
FFI_FUNCTION(void, copyToByteArray, (ByteArrayHandle val, const uint8_t *buf, ffi_size_t len), buf, len)
/** @brief Gets the size of the ByteArray in bytes */
FFI_FUNCTION(ffi_size_t, getByteArraySize, (ByteArrayHandle val))
/** @brief Gets a pointer to the raw internal data of the ByteArray */
FFI_FUNCTION(void *, getByteArrayDataPtr, (ByteArrayHandle val))
/** @brief Pushes a single byte to the end of the ByteArray */
FFI_FUNCTION(void, pushToByteArray, (ByteArrayHandle val, uint8_t byte), val, byte)
/** @brief Gets the byte at a specific index in the ByteArray */
FFI_FUNCTION(uint8_t, getInByteArray, (ByteArrayHandle val, ffi_size_t index), val, index)
/** @brief Sets a byte at a specific index in the ByteArray */
FFI_FUNCTION(void, setInByteArray, (ByteArrayHandle val, ffi_size_t index, uint8_t byte), val, index, byte)

/* --- SER MESSAGE INTERFACE --- */

/** @brief Creates a new serialized message container */
FFI_FUNCTION(SerMessageHandle, createSerMessage, (), )
/** @brief Destroys a serialized message */
FFI_FUNCTION(void, destroySerMessage, (SerMessageHandle val), val)
/** @brief Serializes a message into a ByteArray payload */
FFI_FUNCTION(ByteArrayHandle, serializeSerMessage, (SerMessageHandle val), val)
/** @brief Deserializes raw bytes into a SerMessageHandle */
FFI_FUNCTION(bool, deserializeSerMessage, (const uint8_t *buf, ffi_size_t len, SerMessageHandle out_val), buf, len, out_val)
/** @brief Deserializes from a ByteArray handle directly */
FFI_FUNCTION(bool, deserializeSerMessageFromByteArray, (ByteArrayHandle val, SerMessageHandle out_val), val, out_val)

/* --- SER VALUE INTERFACE --- */

/** @brief Creates a new serialized value dictionary */
FFI_FUNCTION(SerValueHandle, createSerValue, (), )
/** @brief Destroys a serialized value */
FFI_FUNCTION(void, destroySerValue, (SerValueHandle val), val)
/** @brief Serializes a value dictionary into a ByteArray */
FFI_FUNCTION(ByteArrayHandle, serializeSerValue, (SerValueHandle val), val)
/** @brief Deserializes raw bytes into a SerValueHandle */
FFI_FUNCTION(bool, deserializeSerValue, (const uint8_t *buf, ffi_size_t len, SerValueHandle out_val), buf, len, out_val)
/** @brief Deserializes a ByteArray into a SerValueHandle */
FFI_FUNCTION(bool, deserializeSerValueFromByteArray, (ByteArrayHandle val, SerValueHandle out_val), val, out_val)

/* --- LOGGER INTERFACE --- */
#ifndef LUXON_SERVER_USE_SPDLOG
/** @brief Enables or disables routing logs to the FFI customLogSink */
FFI_FUNCTION(void, luxonEnableCustomLogSink, (bool enable), enable)
#endif
/** @brief Instantiates or retrieves a named logger */
FFI_FUNCTION(LoggerHandle, getOrCreateLogger, (const char *name), name)
/** @brief Sets the severity level for the logger */
FFI_FUNCTION(void, setLoggerLevel, (LoggerHandle logger, int32_t level), logger, level)
/** @brief Emits a TRACE level log message */
FFI_FUNCTION(void, loggerTrace, (LoggerHandle logger, const char *message), logger, message)
/** @brief Emits a DEBUG level log message */
FFI_FUNCTION(void, loggerDebug, (LoggerHandle logger, const char *message), logger, message)
/** @brief Emits an INFO level log message */
FFI_FUNCTION(void, loggerInfo, (LoggerHandle logger, const char *message), logger, message)
/** @brief Emits a WARN level log message */
FFI_FUNCTION(void, loggerWarn, (LoggerHandle logger, const char *message), logger, message)
/** @brief Emits an ERROR level log message */
FFI_FUNCTION(void, loggerError, (LoggerHandle logger, const char *message), logger, message)
/** @brief Emits a CRITICAL level log message */
FFI_FUNCTION(void, loggerCritical, (LoggerHandle logger, const char *message), logger, message)

/* --- PEER INTERFACE --- */

/** @brief Returns true if the peer has been successfully authenticated */
FFI_FUNCTION(bool, peerIsAuthenticated, (PeerHandle peer), peer)
/** @brief Gets the underlying network protocol of the peer connection */
FFI_FUNCTION(LuxonServerProtocol, peerGetTransportProtocol, (PeerHandle peer), peer)
/** @brief Sends a payload across a specified channel to the peer */
FFI_FUNCTION(void, peerSend, (PeerHandle peer, const uint8_t *payload, ffi_size_t length, uint8_t channel, LuxonDeliveryMode delivery_mode), peer, payload,
             length, channel, delivery_mode)
/** @brief Disconnects the peer from the server */
FFI_FUNCTION(void, peerDisconnect, (PeerHandle peer), peer)
/** @brief Gets the user ID associated with the user/peer */
FFI_FUNCTION(const char *, peerGetUserId, (PeerHandle peer), peer)
/** @brief Gets the handle of the Game the peer is currently residing in */
FFI_FUNCTION(GameHandle, peerGetCurrentGame, (PeerHandle peer), peer)
/** @brief Gets the handle of the Game the peer is invited to join */
FFI_FUNCTION(GameHandle, peerGetInvitedGame, (PeerHandle peer), peer)

/* --- GAME PEER INTERFACE --- */

/** @brief Allocates an empty GamePeerHandle container */
FFI_FUNCTION(GamePeerHandle, createGamePeerContainer, (), )
/** @brief Destroys a GamePeerHandle container */
FFI_FUNCTION(void, destroyGamePeerContainer, (GamePeerHandle game_peer), game_peer)
/** @brief Returns true if the GamePeer points to a valid active user */
FFI_FUNCTION(bool, gamePeerIsValid, (GamePeerHandle game_peer), game_peer)
/** @brief Returns the Actor ID of the game peer inside the room */
FFI_FUNCTION(int32_t, gamePeerGetActorId, (GamePeerHandle game_peer), game_peer)
/** @brief Checks if a peer subscribes to a specific interest group */
FFI_FUNCTION(bool, gamePeerHasInterestGroup, (GamePeerHandle game_peer, uint8_t group), game_peer, group)
/** @brief Subscribes or unsubscribes the peer to/from an interest group */
FFI_FUNCTION(void, gamePeerSetInterestGroup, (GamePeerHandle game_peer, uint8_t group, bool enable), game_peer, group, enable)
/** @brief Retrieves the interest group bitmask */
FFI_FUNCTION(void, gamePeerGetInterestGroupsMask, (GamePeerHandle game_peer, uint32_t *out_mask_words, ffi_size_t max_words), game_peer, out_mask_words,
             max_words)
/** @brief Sets the interest group bitmask */
FFI_FUNCTION(void, gamePeerSetInterestGroupsMask, (GamePeerHandle game_peer, const uint32_t *mask_words, ffi_size_t word_count), game_peer, mask_words,
             word_count)

/** @brief Populates out_props with the custom properties dictionary of this actor */
FFI_FUNCTION(void, gamePeerGetActorProps, (GamePeerHandle game_peer, SerValueHandle out_props), game_peer, out_props)
/** @brief Directly sets custom properties for this actor */
FFI_FUNCTION(void, gamePeerSetActorProps, (GamePeerHandle game_peer, SerValueHandle props), game_peer, props)
/** @brief Retrieves the underlying physical PeerHandle */
FFI_FUNCTION(PeerHandle, gamePeerGetBasePeer, (GamePeerHandle game_peer), game_peer)
/** @brief Disconnects the game peer. Returns true on success */
FFI_FUNCTION(bool, gamePeerDisconnect, (GamePeerHandle game_peer), game_peer)

/* --- EVENT INTERFACE --- */

/** @brief Creates a new Event packet container */
FFI_FUNCTION(EventHandle, createEvent, (), )
/** @brief Destroys an Event container */
FFI_FUNCTION(void, destroyEvent, (EventHandle event), event)
/** @brief Configures the fundamental routing mechanisms of an event */
FFI_FUNCTION(void, eventSetRoutingMetadata,
             (EventHandle event, uint8_t code, int32_t sender_actor_id, LuxonDeliveryMode delivery_mode, uint8_t channel, uint8_t interest_group), event, code,
             sender_actor_id, delivery_mode, channel, interest_group)
/** @brief Extracts the routing parameters of a given event */
FFI_FUNCTION(void, eventGetRoutingMetadata,
             (EventHandle event, uint8_t *out_code, int32_t *out_sender_id, LuxonDeliveryMode *out_mode, uint8_t *out_channel, uint8_t *out_group), event,
             out_code, out_sender_id, out_mode, out_channel, out_group)
/** @brief Sets the event to be broadcasted to all users in the room */
FFI_FUNCTION(void, eventSetReceiversAll, (EventHandle event), event)
/** @brief Sets the event to be broadcasted only to users within an interest group */
FFI_FUNCTION(void, eventSetReceiversGroup, (EventHandle event, uint8_t group), event, group)
/** @brief Sets the event to be delivered to a specific list of Actor IDs */
FFI_FUNCTION(void, eventSetReceiversActors, (EventHandle event, const int32_t *actor_ids, ffi_size_t count), event, actor_ids, count)
/** @brief Gets the configured receiver type for the event */
FFI_FUNCTION(LuxonEventReceiversType, eventGetReceiversType, (EventHandle event), event)
/** @brief Gets the target interest group of the event */
FFI_FUNCTION(uint8_t, eventGetReceiversGroup, (EventHandle event), event)
/** @brief Extracts the specific array of Actor IDs the event targets */
FFI_FUNCTION(void, eventGetReceiversActors, (EventHandle event, int32_t *out_actor_ids, ffi_size_t max_count, ffi_size_t *out_written), event, out_actor_ids,
             max_count, out_written)
/** @brief Populates the event's data payload */
FFI_FUNCTION(void, eventSetData, (EventHandle event, SerValueHandle data), event, data)
/** @brief Extracts the data payload from the event */
FFI_FUNCTION(void, eventGetData, (EventHandle event, SerValueHandle out_data), event, out_data)
/** @brief Sets top-level operation parameters */
FFI_FUNCTION(void, eventSetTopParams, (EventHandle event, SerValueHandle top_params), event, top_params)
/** @brief Gets top-level operation parameters */
FFI_FUNCTION(void, eventGetTopParams, (EventHandle event, SerValueHandle out_top_params), event, out_top_params)

/* --- GAME INTERFACE --- */

/** @brief Gets the Game's unique name/ID */
FFI_FUNCTION(void, gameGetId, (GameHandle game, char *out_id, ffi_size_t max_len), game, out_id, max_len)
/** @brief Retrieves the core configuration flags of the game */
FFI_FUNCTION(void, gameGetConfigState,
             (GameHandle game, uint8_t *out_flags, bool *out_is_created, bool *out_is_open, bool *out_is_visible, uint8_t *out_max_peers,
              int32_t *out_master_actor),
             game, out_flags, out_is_created, out_is_open, out_is_visible, out_max_peers, out_master_actor)
/** @brief Modifies core state variables like visibility and open status */
FFI_FUNCTION(void, gameSetConfigState, (GameHandle game, uint8_t flags, bool is_open, bool is_visible, uint8_t max_peers, int32_t master_actor), game, flags,
             is_open, is_visible, max_peers, master_actor)
/** @brief Gets the Actor ID mapped to the master client */
FFI_FUNCTION(int32_t, gameGetMasterActor, (GameHandle game), game)
/** @brief Reassigns the master client to a new Actor ID */
FFI_FUNCTION(void, gameSetMasterActor, (GameHandle game, int32_t actor_id), game, actor_id)
/** @brief Gets the latest Actor ID value allocated */
FFI_FUNCTION(int32_t, gameGetLastActorId, (GameHandle game), game)
/** @brief Gets the maximum peer limit of the room */
FFI_FUNCTION(uint8_t, gameGetMaxPeers, (GameHandle game), game)
/** @brief Dynamically changes the maximum peer limit */
FFI_FUNCTION(void, gameSetMaxPeers, (GameHandle game, uint8_t max_peers), game, max_peers)
/** @brief Gets the configured Player TTL and Empty Room TTL */
FFI_FUNCTION(void, gameGetTtlConfig, (GameHandle game, int32_t *out_player_ttl, int32_t *out_empty_ttl), game, out_player_ttl, out_empty_ttl)
/** @brief Updates the TTL parameters for the room */
FFI_FUNCTION(void, gameSetTtlConfig, (GameHandle game, int32_t player_ttl, int32_t empty_ttl), game, player_ttl, empty_ttl)
/** @brief Retrieves all custom room properties into the provided SerValueHandle */
FFI_FUNCTION(void, gameGetCustomProps, (GameHandle game, SerValueHandle out_custom_props), game, out_custom_props)
/** @brief Batch overrides custom room properties */
FFI_FUNCTION(void, gameSetCustomProps, (GameHandle game, SerValueHandle custom_props), game, custom_props)
/** @brief Extracts properties exposed to the Lobby */
FFI_FUNCTION(void, gameGetLobbyPropsToSerValue, (GameHandle game, SerValueHandle out_keys_array), game, out_keys_array)
/** @brief Sets which properties are visible in the Lobby view */
FFI_FUNCTION(void, gameSetLobbyPropsFromSerValue, (GameHandle game, SerValueHandle keys_array), game, keys_array)
/** @brief Dumps expected users array to a SerValueHandle list */
FFI_FUNCTION(void, gameGetExpectedUsersToSerValue, (GameHandle game, SerValueHandle out_list), game, out_list)
/** @brief Populates the expected users array from a SerValueHandle list */
FFI_FUNCTION(void, gameSetExpectedUsersFromSerValue, (GameHandle game, SerValueHandle list), game, list)
/** @brief Reserves a spot for a User ID */
FFI_FUNCTION(void, gameAddExpectedUser, (GameHandle game, const char *user_id), game, user_id)
/** @brief Removes a reservation for a User ID */
FFI_FUNCTION(void, gameRemoveExpectedUser, (GameHandle game, const char *user_id), game, user_id)
/** @brief Gets the count of peers currently joined */
FFI_FUNCTION(ffi_size_t, gameGetPeerCount, (GameHandle game), game)
/** @brief Creates a game peer wrapper for a base peer */
FFI_FUNCTION(GamePeerHandle, gameCreatePeer, (GameHandle game, PeerHandle peer), game, peer)
/** @brief Inserts a game peer securely into the game tracking */
FFI_FUNCTION(GamePeerHandle, gameAddPeer, (GameHandle game, GamePeerHandle game_peer), game, game_peer)
/** @brief Removes a peer from the game tracking by Base Peer handle */
FFI_FUNCTION(bool, gameRemovePeer, (GameHandle game, PeerHandle peer), game, peer)
/** @brief Floods the game peer with cached events */
FFI_FUNCTION(bool, gameFloodPeer, (GameHandle game, GamePeerHandle game_peer), game, game_peer)
/** @brief Resolves a GamePeerHandle from an Actor ID */
FFI_FUNCTION(GamePeerHandle, gameFindPeerByActorId, (GameHandle game, int32_t actor_id), game, actor_id)
/** @brief Resolves a GamePeerHandle from a Base Peer wrapper */
FFI_FUNCTION(GamePeerHandle, gameFindPeerByBasePeer, (GameHandle game, PeerHandle peer), game, peer)
/** @brief Dispatches an event according to its routing parameters */
FFI_FUNCTION(void, gameBroadcastEvent, (GameHandle game, EventHandle event), game, event)
/** @brief Validates if a user is legally allowed to join */
FFI_FUNCTION(bool, gameValidateJoin,
             (GameHandle game, const char *user_id, ffi_size_t new_expected_users_count, int16_t *out_err_code, char *out_err_str, ffi_size_t max_err_len),
             game, user_id, new_expected_users_count, out_err_code, out_err_str, max_err_len)
/** @brief Manually pushes updated room variables to the Lobby */
FFI_FUNCTION(void, gameTriggerLobbyUpdate, (GameHandle game), game)
/** @brief Fetches a single specific property from the game */
FFI_FUNCTION(void, gameGetGameProp, (GameHandle game, SerValueHandle key, SerValueHandle out_val), game, key, out_val)
/** @brief Returns all properties that have Lobby exposure */
FFI_FUNCTION(void, gameGetLobbyGameProps, (GameHandle game, SerValueHandle out_hashtable), game, out_hashtable)
/** @brief Retrieves the overarching property map (including custom optionally) */
FFI_FUNCTION(void, gameGetGameProps, (GameHandle game, bool no_custom, SerValueHandle out_hashtable), game, no_custom, out_hashtable)
/** @brief Generates a hashtable of all properties bound to users */
FFI_FUNCTION(void, gameGetActorProps, (GameHandle game, SerValueHandle out_hashtable), game, out_hashtable)
/** @brief Updates the properties dictionary of the game */
FFI_FUNCTION(void, gameInsertGameProps, (GameHandle game, SerValueHandle update), game, update)
/** @brief Performs Compare And Swap validations on game properties */
FFI_FUNCTION(bool, gameExpectGameProps, (GameHandle game, SerValueHandle expected), game, expected)
/** @brief Updates the properties of a specific Actor */
FFI_FUNCTION(bool, gameInsertActorProps, (GameHandle game, int32_t actor_id, SerValueHandle update), game, actor_id, update)
/** @brief Performs Compare And Swap validations on Actor properties */
FFI_FUNCTION(bool, gameExpectActorProps, (GameHandle game, int32_t actor_id, SerValueHandle expected), game, actor_id, expected)
/** @brief Used for Matchmaking to check if properties match an arbitrary filter */
FFI_FUNCTION(bool, gameMatchesFilter, (SerValueHandle event_data, SerValueHandle filter), event_data, filter)

/* --- LOBBY INTERFACE --- */

/** @brief Instantiates a Lobby wrapper */
FFI_FUNCTION(LobbyHandle, createLobby, (const char *name, uint8_t type), name, type)
/** @brief Destroys the Lobby wrapper */
FFI_FUNCTION(void, destroyLobby, (LobbyHandle lobby), lobby)
/** @brief Creates a game via Lobby orchestration */
FFI_FUNCTION(GameHandle, lobbyCreateGame, (LobbyHandle lobby, const char *id, bool or_get, SerMessageHandle out_err_resp), lobby, id, or_get, out_err_resp)
/** @brief Returns global peer count tracked by the lobby */
FFI_FUNCTION(ffi_size_t, lobbyGetPeerCount, (LobbyHandle lobby), lobby)
/** @brief Returns the peer count of users tracking via the Master Server */
FFI_FUNCTION(ffi_size_t, lobbyGetMasterPeerCount, (LobbyHandle lobby), lobby)
/** @brief Evaluates an SQL style matchmaking query against lobby rooms */
FFI_FUNCTION(bool, lobbyQueryLobbies, (LobbyHandle lobby, const char *sql_queries, char *out_buf, ffi_size_t max_len, ffi_size_t *out_written), lobby,
             sql_queries, out_buf, max_len, out_written)

/* --- SERVER MANAGER CONFIG BUILDER INTERFACE --- */

/** @brief Instantiates a blank configuration builder */
FFI_FUNCTION(ServerManagerConfigHandle, createServerManagerConfig, (), )
/** @brief Destroys a config builder */
FFI_FUNCTION(void, destroyServerManagerConfig, (ServerManagerConfigHandle config), config)
/** @brief Loads server configurations directly from a file path */
FFI_FUNCTION(ServerManagerConfigHandle, loadServerManagerConfigFromFile, (const char *config_file_path), config_file_path)
/** @brief Loads server configurations from raw YAML buffer */
FFI_FUNCTION(ServerManagerConfigHandle, parseServerManagerConfig, (const char *yaml_config_contents), yaml_config_contents)
/** @brief Appends a server to the config */
FFI_FUNCTION(void, serverManagerConfigAddServer, (ServerManagerConfigHandle config, LuxonServerType type, uint16_t port), config, type, port)
/** @brief Appends a server and adds its endpoint */
FFI_FUNCTION(void, serverManagerConfigAddServerAndEndpoint,
             (ServerManagerConfigHandle config, LuxonServerType type, uint16_t port, const char *external_udp_address), config, type, port,
             external_udp_address)
/** @brief Tunes low-level networking parameters (IPv6, Connections, Ticks) */
FFI_FUNCTION(void, serverManagerConfigSetLimits,
             (ServerManagerConfigHandle config, bool enable_ipv6, unsigned max_connections, unsigned max_game_peers, uint32_t tick_time_budget), config,
             enable_ipv6, max_connections, max_game_peers, tick_time_budget)
/** @brief Assigns the SQLite target path */
FFI_FUNCTION(void, serverManagerConfigSetSettingsDatabasePath, (ServerManagerConfigHandle config, const char *path), config, path)
/** @brief Activates the builtin HTTP Server */
FFI_FUNCTION(void, serverManagerConfigEnableHttp, (ServerManagerConfigHandle config, const char *address, uint16_t port), config, address, port)
/** @brief Deactivates the HTTP Server */
FFI_FUNCTION(void, serverManagerConfigDisableHttp, (ServerManagerConfigHandle config), config)

/* --- SERVER MANAGER INTERFACE --- */

/** @brief Instantiates a Server Manager from a fully populated config object. Invalidates the config object */
FFI_FUNCTION(ServerManagerHandle, createServerManagerFromConfig, (ServerManagerConfigHandle config), config)
/** @brief High level shortcut to instantiate directly from a YAML file */
FFI_FUNCTION(ServerManagerHandle, createServerManagerFromFile, (const char *config_file_path), config_file_path)
/** @brief High level shortcut to instantiate directly from a YAML payload */
FFI_FUNCTION(ServerManagerHandle, createServerManagerFromContents, (const char *yaml_config_contents), yaml_config_contents)
/** @brief High level shortcut to instantiate directly from an IPC file descriptor */
FFI_FUNCTION(ServerManagerHandle, createServerManagerFromIPC, (const char *ipc_fd), ipc_fd)
/** @brief Destroys the main manager instance */
FFI_FUNCTION(void, destroyServerManager, (ServerManagerHandle manager), manager)
/** @brief Executes the server loop infinitely until stopped */
FFI_FUNCTION(void, serverManagerRun, (ServerManagerHandle manager), manager)
/** @brief Ticks the server loop one single step (useful for custom schedulers) */
FFI_FUNCTION(bool, serverManagerRunOnce, (ServerManagerHandle manager), manager)
/** @brief Initiates graceful shutdown sequences */
FFI_FUNCTION(void, serverManagerStop, (ServerManagerHandle manager), manager)
/** @brief Locates the address string of a designated endpoint */
FFI_FUNCTION(bool, serverManagerGetEndpointOf,
             (ServerManagerHandle manager, LuxonServerType server_type, LuxonServerProtocol server_proto, char *out_address, ffi_size_t max_len), manager,
             server_type, server_proto, out_address, max_len)
/** @brief Retrieves global connected user count */
FFI_FUNCTION(ffi_size_t, serverManagerGetConnectionCount, (ServerManagerHandle manager), manager)
/** @brief Retrieves max global peers */
FFI_FUNCTION(unsigned, serverManagerGetMaxConnections, (ServerManagerHandle manager), manager)
/** @brief Retrieves maximum peers per game */
FFI_FUNCTION(uint8_t, serverManagerGetMaxGamePeers, (ServerManagerHandle manager), manager)
/** @brief Enables multiprocessing via multiProcessingLaunchIPCChild import */
FFI_FUNCTION(bool, serverManagerEnableMultiprocessing, (ServerManagerHandle manager), manager)

/* --- HANDLER / GAME EXTENSIONS --- */

/** @brief Retrieves the active ServerManager from a Handler context */
FFI_FUNCTION(ServerManagerHandle, handlerBaseGetServerManager, (HandlerBaseHandle handler), handler)
/** @brief Retrieves the active ServerManager from a Game context */
FFI_FUNCTION(ServerManagerHandle, gameGetServerManager, (GameHandle game), game)

/* --- COMMAND RESTARTER INTERFACE --- */

/** @brief Checks if an ongoing command is instructed to abort */
FFI_FUNCTION(bool, serverManagerShouldAbortActiveCommand, (ServerManagerHandle manager), manager)
/** @brief Marks current operations as at least partially flushed/committed to prevent command restart */
FFI_FUNCTION(void, serverManagerMarkCommandCommited, (ServerManagerHandle manager), manager)
#ifdef LUXON_SERVER_ENABLE_COMMAND_RESTARTER
/** @brief Cancels current command and generates an object capable of restarting it */
FFI_FUNCTION(CommandRestarterHandle, serverManagerTakeCommandRestarter, (ServerManagerHandle manager), manager)
/** @brief Restarts the command and returns when command has completed, failed, or has been aborted again */
FFI_FUNCTION(void, commandRestarterRestart, (CommandRestarterHandle restarter), restarter)
/** @brief Destroys the restarter handle */
FFI_FUNCTION(void, destroyCommandRestarter, (CommandRestarterHandle restarter), restarter)
#endif

/* --- PLUGINS / HOOKPOINTS REGISTRATION INTERFACE --- */
#ifdef LUXON_SERVER_ENABLE_PLUGINS
/** @brief Injects a Game Plugin ID globally to route interceptions correctly */
FFI_FUNCTION(uint32_t, registerGamePlugin, (const char *plugin_name), plugin_name)
/** @brief Binds standard Auth provider interceptions */
FFI_FUNCTION(bool, registerAuthPlugin, (unsigned type), type)
#endif
#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
/** @brief Registers the internal hookpoint routing for a server instance */
FFI_FUNCTION(void, registerHookpoints, (ServerManagerHandle manager), manager)
#endif

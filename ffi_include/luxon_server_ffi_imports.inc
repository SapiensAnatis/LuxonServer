// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

/*
 * LUXON_SERVER_ENABLE_PLUGINS
 * - EXPORTS: registerGamePlugin
 *            registerAuthPlugin
 * - IMPORTS: gamePlugin*
 *            authPluginOnAuthenticate
 *
 * !LUXON_SERVER_USE_SPDLOG
 * - EXPORTS: luxonEnableCustomLogSink
 * - IMPORTS: customLogSink
 *
 * LUXON_SERVER_ENABLE_HOOKPOINTS
 * - EXPORTS: registerHookpoints
 * - IMPORTS: hookpointMasterServerHandleOperationRequestJoinGame
 *            hookpointMasterServerHandleOperationRequestCreateGame
 *            hookpointHandlerBaseHandleENetCommandOnMessage
 *            hookpointAppLoadAppSettings
 */

#ifndef FFI_IMPORT
#if defined(FFI_WASM) || defined(__wasm__)
#define FFI_IMPORT(ret, name, params, ...) __attribute__((import_module("env"), import_name(#name))) extern ret name params;
#else
#define FFI_IMPORT(ret, name, params, ...) ret(*name) params;
#endif
#endif

#ifndef FFI_IMPORT_PAD
#if defined(FFI_WASM) || defined(__wasm__)
// WASM uses module imports, no struct padding needed
#define FFI_IMPORT_PAD(name)
#else
// Native builds need a void pointer to preserve struct offsets
#define FFI_IMPORT_PAD(name) void* _pad_##name;
#endif
#endif

#ifdef LUXON_SERVER_ENABLE_PLUGINS
/* --- GAME PLUGIN IMPORTS --- */

/**
 * @brief Called after the plugin instance is created and attached to a Game instance
 * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the game room this plugin is attached to
 */
FFI_IMPORT(void, gamePluginOnAttach, (uint32_t plugin_id, GameHandle game), plugin_id, game)

/**
 * @brief Called when a peer attempts to create a new game/room
 * * Allows the plugin to intercept the creation request, validate parameters, or deny the creation entirely
 * * @param plugin_id The unique identifier for this plugin instance
 * @param game The underlying GameHandle that is being created
 * @param req The SerMessageHandle containing the request payload
 * @param creator The PeerHandle of the user creating the game
 * @param is_join True if the request is a combined CreateOrJoin operation
 * @param create_if_not_exist True if the operation specifies to create the game if it doesn't already exist
 * @return A LuxonGamePluginResult value (0 = Continue, 1 = Fail, 2 = Cancel)
 */
FFI_IMPORT(uint8_t, gamePluginOnCreateGame,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, PeerHandle creator, bool is_join, bool create_if_not_exist), plugin_id, game, req,
           creator, is_join, create_if_not_exist)

/**
 * @brief Called before a peer actually joins the game/room
 * * Used to accept or reject a user based on custom logic (e.g. bans, matchmaking constraints)
 * * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the current game
 * @param req The SerMessageHandle containing the join request data
 * @param joiner The PeerHandle attempting to join the game
 * @return A LuxonGamePluginResult value (0 = Continue, 1 = Fail, 2 = Cancel)
 */
FFI_IMPORT(uint8_t, gamePluginBeforeJoin, (uint32_t plugin_id, GameHandle game, SerMessageHandle req, PeerHandle joiner), plugin_id, game, req, joiner)

/**
 * @brief Called immediately after a peer has successfully joined the game/room
 * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the current game
 * @param req The SerMessageHandle containing the initial request data
 * @param joiner The GamePeerHandle of the joined peer
 * @return A LuxonGamePluginResult value indicating whether to continue normal post-join processing
 */
FFI_IMPORT(uint8_t, gamePluginOnJoinGame, (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle joiner), plugin_id, game, req, joiner)

/**
 * @brief Called when a peer leaves or disconnects from the game/room
 * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the current game
 * @param req The SerMessageHandle of the leave request (if explicit)
 * @param leaver The GamePeerHandle of the peer leaving
 * @return A LuxonGamePluginResult value
 */
FFI_IMPORT(uint8_t, gamePluginOnLeave, (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle leaver), plugin_id, game, req, leaver)

/**
 * @brief Called when a client sends a custom event (RaiseEvent) to the game
 * * Enables the interception, modification, or blocking of custom events before they are broadcasted to other peers or cached
 * * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the current game
 * @param req The SerMessageHandle of the raise event request
 * @param raiser The GamePeerHandle of the peer that sent the event
 * @param event The EventHandle representing the intercepted event
 * @param cache_op The caching operation mode defined by the client
 * @return A LuxonGamePluginResult value (0 = Continue event broadcast, 1 = Fail, 2 = Cancel broadcast)
 */
FFI_IMPORT(uint8_t, gamePluginOnRaiseEvent,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle raiser, EventHandle event, uint8_t cache_op), plugin_id, game, req,
           raiser, event, cache_op)

/**
 * @brief Called before game or actor properties are modified
 * * The plugin can validate the property changes or reject them entirely
 * * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the current game
 * @param req The SerMessageHandle of the property set request
 * @param setter The GamePeerHandle of the peer attempting the change
 * @param broadcast Indicates whether this property change will be broadcasted to clients
 * @param target_actor_id The actor ID whose properties are changing, or 0 if game properties
 * @param update The SerValueHandle containing the incoming property updates
 * @param expected The SerValueHandle containing the expected properties (CAS - Compare And Swap)
 * @return A LuxonGamePluginResult value
 */
FFI_IMPORT(uint8_t, gamePluginBeforeSetProperties,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle setter, bool broadcast, int32_t target_actor_id, SerValueHandle update,
            SerValueHandle expected),
           plugin_id, game, req, setter, broadcast, target_actor_id, update, expected)

/**
 * @brief Called after game or actor properties have successfully been modified
 * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the current game
 * @param req The SerMessageHandle of the request
 * @param setter The GamePeerHandle of the peer who made the change
 * @param broadcast Indicates whether this property change was broadcasted
 * @param target_actor_id The actor ID whose properties changed, or 0 if game properties
 * @param update The SerValueHandle containing the applied properties
 * @param expected The SerValueHandle containing the expected properties checks
 * @return A LuxonGamePluginResult value
 */
FFI_IMPORT(uint8_t, gamePluginOnSetProperties,
           (uint32_t plugin_id, GameHandle game, SerMessageHandle req, GamePeerHandle setter, bool broadcast, int32_t target_actor_id, SerValueHandle update,
            SerValueHandle expected),
           plugin_id, game, req, setter, broadcast, target_actor_id, update, expected)

/**
 * @brief Called just before the game/room is forcefully closed and removed from memory
 * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the current game
 * @param failed_on_create Indicates if the game is closing because the creation sequence failed
 * @return A LuxonGamePluginResult value
 */
FFI_IMPORT(uint8_t, gamePluginBeforeCloseGame, (uint32_t plugin_id, GameHandle game, bool failed_on_create), plugin_id, game, failed_on_create)

/**
 * @brief Called after the game/room has been completely closed and all resources are ready for cleanup
 * @param plugin_id The unique identifier for this plugin instance
 * @param game The GameHandle representing the closed game
 * @param failed_on_create Indicates if the game closed because creation failed
 * @return A LuxonGamePluginResult value
 */
FFI_IMPORT(uint8_t, gamePluginOnCloseGame, (uint32_t plugin_id, GameHandle game, bool failed_on_create), plugin_id, game, failed_on_create)

/* --- AUTH PLUGIN IMPORTS --- */

/**
 * @brief Intercepts authentication requests allowing custom logic for verifying users
 * @param type The type of authentication provider requested
 * @param requested_user_id The User ID supplied by the client
 * @param params Additional authentication parameters (URL encoded)
 * @param data Data payload accompanying the auth request
 * @param secret Secret token/password used for authentication
 * @param auth_url Custom auth URL requested by the client
 * @param out_user_id Output buffer where the verified unique User ID should be written
 * @param max_user_id_len Maximum length of the out_user_id buffer
 * @param out_err_resp If authentication fails, a serialized error message can be written here
 * @return True if authentication succeeded, false otherwise
 */
FFI_IMPORT(bool, authPluginOnAuthenticate,
           (unsigned type, const char *requested_user_id, const char *params, const char *data, const char *secret, const char *auth_url, char *out_user_id,
            ffi_size_t max_user_id_len, SerMessageHandle out_err_resp),
           type, requested_user_id, params, data, secret, auth_url, out_user_id, max_user_id_len, out_err_resp)
#else
/* --- PLUGINS PADDING --- */
FFI_IMPORT_PAD(gamePluginOnAttach)
FFI_IMPORT_PAD(gamePluginOnCreateGame)
FFI_IMPORT_PAD(gamePluginBeforeJoin)
FFI_IMPORT_PAD(gamePluginOnJoinGame)
FFI_IMPORT_PAD(gamePluginOnLeave)
FFI_IMPORT_PAD(gamePluginOnRaiseEvent)
FFI_IMPORT_PAD(gamePluginBeforeSetProperties)
FFI_IMPORT_PAD(gamePluginOnSetProperties)
FFI_IMPORT_PAD(gamePluginBeforeCloseGame)
FFI_IMPORT_PAD(gamePluginOnCloseGame)
FFI_IMPORT_PAD(authPluginOnAuthenticate)
#endif

#ifndef LUXON_SERVER_USE_SPDLOG
/* --- LOGGER IMPORTS --- */

/**
 * @brief Custom log sink interceptor
 * @param level The log severity level mapped to LuxonLogLevel
 * @param message The formatted log message
 */
FFI_IMPORT(void, customLogSink, (int32_t level, const char *message), level, message)
#else
/* --- LOGGER PADDING --- */
FFI_IMPORT_PAD(customLogSink)
#endif

#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
/* --- HOOKPOINTS IMPORTS --- */

/**
 * @brief Intercepts MasterServer requests for joining a game
 * @param handler The base server handler
 * @param game_id The ID of the requested game
 * @param is_join True if it's a direct join, False if it's potentially a CreateOrJoin
 * @return True to allow normal execution, False to halt processing
 */
FFI_IMPORT(bool, hookpointMasterServerHandleOperationRequestJoinGame, (HandlerBaseHandle handler, const char *game_id, bool is_join), handler, game_id, is_join)

/**
 * @brief Intercepts MasterServer requests for creating a game
 * @param handler The base server handler
 * @param game_id The requested ID for the new game
 * @return True to allow normal execution, False to halt processing
 */
FFI_IMPORT(bool, hookpointMasterServerHandleOperationRequestCreateGame, (HandlerBaseHandle handler, const char *game_id), handler, game_id)

/**
 * @brief Low-level interceptor for raw ENet messages before handler processing
 * @param handler The base server handler
 * @param message The deserialized incoming message
 * @return True to allow normal execution, False to drop the message
 */
FFI_IMPORT(bool, hookpointHandlerBaseHandleENetCommandOnMessage, (HandlerBaseHandle handler, SerMessageHandle message), handler, message)

/**
 * @brief Intercepts the loading of application settings
 * @param app Pointer to the application instance
 * @param out_success Out parameter indicating if the loading succeeded
 * @return True to allow normal loading, False to bypass native loading
 */
FFI_IMPORT(bool, hookpointAppLoadAppSettings, (void *app, bool *out_success), app, out_success)
#else
/* --- HOOKPOINTS PADDING --- */
FFI_IMPORT_PAD(hookpointMasterServerHandleOperationRequestJoinGame)
FFI_IMPORT_PAD(hookpointMasterServerHandleOperationRequestCreateGame)
FFI_IMPORT_PAD(hookpointHandlerBaseHandleENetCommandOnMessage)
FFI_IMPORT_PAD(hookpointAppLoadAppSettings)
#endif

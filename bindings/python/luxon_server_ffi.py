import ctypes
import warnings
from ctypes import (
    c_int, c_uint, c_int16, c_int32, c_uint8, c_uint16, c_uint32,
    c_size_t, c_ssize_t, c_void_p, c_char_p, POINTER, Structure, CFUNCTYPE
)

# Types
ffi_size_t = c_size_t
ffi_ssize_t = c_ssize_t

c_bool_t = c_uint8

# Handle Types
ServerManagerHandle = c_void_p
ServerManagerConfigHandle = c_void_p
GameHandle = c_void_p
LobbyHandle = c_void_p
PeerHandle = c_void_p
GamePeerHandle = c_void_p
EventHandle = c_void_p
LoggerHandle = c_void_p
ByteArrayHandle = c_void_p
SerValueHandle = c_void_p
SerMessageHandle = c_void_p
HandlerBaseHandle = c_void_p

# Enums
LuxonServerType = c_int
LUXON_SERVER_TYPE_NONE = 0
LUXON_SERVER_TYPE_NAMESERVER = 1
LUXON_SERVER_TYPE_MASTERSERVER = 2
LUXON_SERVER_TYPE_GAMESERVER = 3

LuxonServerProtocol = c_int
LUXON_PROTOCOL_UDP = 0
LUXON_PROTOCOL_TCP = 1
LUXON_PROTOCOL_WEBSOCKET = 2

LuxonDeliveryMode = c_int
LUXON_DELIVERY_UNRELIABLE = 0
LUXON_DELIVERY_RELIABLE = 1
LUXON_DELIVERY_UNSEQUENCED = 2

LuxonEventReceiversType = c_int
LUXON_RECEIVERS_ALL = 0
LUXON_RECEIVERS_GROUP = 1
LUXON_RECEIVERS_ACTORS = 2

# Game Plugin Callbacks
gamePluginOnAttach_t = CFUNCTYPE(None, c_uint32, GameHandle)
gamePluginOnCreateGame_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, SerMessageHandle, PeerHandle, c_bool_t, c_bool_t)
gamePluginBeforeJoin_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, SerMessageHandle, PeerHandle)
gamePluginOnJoinGame_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, SerMessageHandle, GamePeerHandle)
gamePluginOnLeave_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, SerMessageHandle, GamePeerHandle)
gamePluginOnRaiseEvent_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, SerMessageHandle, GamePeerHandle, EventHandle, c_uint8)
gamePluginBeforeSetProperties_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, SerMessageHandle, GamePeerHandle, c_bool_t, c_int32, SerValueHandle, SerValueHandle)
gamePluginOnSetProperties_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, SerMessageHandle, GamePeerHandle, c_bool_t, c_int32, SerValueHandle, SerValueHandle)
gamePluginBeforeCloseGame_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, c_bool_t)
gamePluginOnCloseGame_t = CFUNCTYPE(c_bool_t, c_uint32, GameHandle, c_bool_t)

# Auth Plugin Callback
authPluginOnAuthenticate_t = CFUNCTYPE(c_bool_t, c_uint, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p, ffi_size_t, SerMessageHandle)

# Hookpoints Callbacks
hookpointMasterServerHandleOperationRequestJoinGame_t = CFUNCTYPE(c_bool_t, HandlerBaseHandle, c_char_p, c_bool_t)
hookpointMasterServerHandleOperationRequestCreateGame_t = CFUNCTYPE(c_bool_t, HandlerBaseHandle, c_char_p)
hookpointHandlerBaseHandleENetCommandOnMessage_t = CFUNCTYPE(c_bool_t, HandlerBaseHandle, SerMessageHandle)
hookpointAppLoadAppSettings_t = CFUNCTYPE(c_bool_t, c_void_p, POINTER(c_bool_t))

# Logger Callback
customLogSink_t = CFUNCTYPE(None, c_int32, c_char_p)


class LuxonServerImports(Structure):
    _fields_ = [
        # Plugins
        ("gamePluginOnAttach", gamePluginOnAttach_t),
        ("gamePluginOnCreateGame", gamePluginOnCreateGame_t),
        ("gamePluginBeforeJoin", gamePluginBeforeJoin_t),
        ("gamePluginOnJoinGame", gamePluginOnJoinGame_t),
        ("gamePluginOnLeave", gamePluginOnLeave_t),
        ("gamePluginOnRaiseEvent", gamePluginOnRaiseEvent_t),
        ("gamePluginBeforeSetProperties", gamePluginBeforeSetProperties_t),
        ("gamePluginOnSetProperties", gamePluginOnSetProperties_t),
        ("gamePluginBeforeCloseGame", gamePluginBeforeCloseGame_t),
        ("gamePluginOnCloseGame", gamePluginOnCloseGame_t),
        ("authPluginOnAuthenticate", authPluginOnAuthenticate_t),

        # Logger
        ("customLogSink", customLogSink_t),

        # Hookpoints
        ("hookpointMasterServerHandleOperationRequestJoinGame", hookpointMasterServerHandleOperationRequestJoinGame_t),
        ("hookpointMasterServerHandleOperationRequestCreateGame", hookpointMasterServerHandleOperationRequestCreateGame_t),
        ("hookpointHandlerBaseHandleENetCommandOnMessage", hookpointHandlerBaseHandleENetCommandOnMessage_t),
        ("hookpointAppLoadAppSettings", hookpointAppLoadAppSettings_t),
    ]

    # Reference to loaded library for feature checking
    _lib_ref = None

    # Map prefixes to C-export that indicates feature is enabled
    _feature_gates = {
        "gamePlugin": "registerGamePlugin",
        "authPlugin": "registerAuthPlugin",
        "customLogSink": "luxonEnableCustomLogSink",
        "hookpoint": "registerHookpoints"
    }

    def __setattr__(self, name, value):
        if value is not None and self._lib_ref is not None:
            # Check if assigned callback's prefix requires a missing feature flag
            for prefix, indicator_export in self._feature_gates.items():
                if name.startswith(prefix) and not hasattr(self._lib_ref, indicator_export):
                    warnings.warn(
                        f"Assignment to '{name}' is dead code. The associated feature ('{indicator_export}') "
                        f"was not compiled into the loaded Luxon FFI library.",
                        RuntimeWarning, stacklevel=2
                    )
                    break
        super().__setattr__(name, value)


# Library binding
def bind_luxon_server(lib: ctypes.CDLL):
    """
    Binds argtypes and restype of provided ctypes library instance
    to correctly interface with Luxon Server C API
    """

    # Give struct access to loaded library to power warnings
    LuxonServerImports._lib_ref = lib

    if hasattr(lib, "luxonSetServerImports"):
        lib.luxonSetServerImports.argtypes = [POINTER(LuxonServerImports)]
        lib.luxonSetServerImports.restype = None

    # Exceptions interface
    lib.luxonHasError.argtypes = []
    lib.luxonHasError.restype = c_bool_t
    lib.luxonGetLastErrorType.argtypes = []
    lib.luxonGetLastErrorType.restype = c_char_p
    lib.luxonGetLastErrorMessage.argtypes = []
    lib.luxonGetLastErrorMessage.restype = c_char_p
    lib.luxonClearLastError.argtypes = []
    lib.luxonClearLastError.restype = None

    # Byte array interface
    lib.createByteArray.argtypes = []
    lib.createByteArray.restype = ByteArrayHandle
    lib.destroyByteArray.argtypes = [ByteArrayHandle]
    lib.destroyByteArray.restype = None
    lib.copyFromByteArray.argtypes = [ByteArrayHandle, POINTER(c_uint8), ffi_size_t]
    lib.copyFromByteArray.restype = ffi_size_t
    lib.appendToByteArray.argtypes = [ByteArrayHandle, POINTER(c_uint8), ffi_size_t]
    lib.appendToByteArray.restype = None
    lib.copyToByteArray.argtypes = [ByteArrayHandle, POINTER(c_uint8), ffi_size_t]
    lib.copyToByteArray.restype = None
    lib.getByteArraySize.argtypes = [ByteArrayHandle]
    lib.getByteArraySize.restype = ffi_size_t
    lib.getByteArrayDataPtr.argtypes = [ByteArrayHandle]
    lib.getByteArrayDataPtr.restype = c_void_p
    lib.pushToByteArray.argtypes = [ByteArrayHandle, c_uint8]
    lib.pushToByteArray.restype = None
    lib.getInByteArray.argtypes = [ByteArrayHandle, ffi_size_t]
    lib.getInByteArray.restype = c_uint8
    lib.setInByteArray.argtypes = [ByteArrayHandle, ffi_size_t, c_uint8]
    lib.setInByteArray.restype = None

    # Ser message interface
    lib.createSerMessage.argtypes = []
    lib.createSerMessage.restype = SerMessageHandle
    lib.destroySerMessage.argtypes = [SerMessageHandle]
    lib.destroySerMessage.restype = None
    lib.serializeSerMessage.argtypes = [SerMessageHandle]
    lib.serializeSerMessage.restype = ByteArrayHandle
    lib.deserializeSerMessage.argtypes = [POINTER(c_uint8), ffi_size_t, SerMessageHandle]
    lib.deserializeSerMessage.restype = c_bool_t
    lib.deserializeSerMessageFromByteArray.argtypes = [ByteArrayHandle, SerMessageHandle]
    lib.deserializeSerMessageFromByteArray.restype = c_bool_t

    # Ser value interface
    lib.createSerValue.argtypes = []
    lib.createSerValue.restype = SerValueHandle
    lib.destroySerValue.argtypes = [SerValueHandle]
    lib.destroySerValue.restype = None
    lib.serializeSerValue.argtypes = [SerValueHandle]
    lib.serializeSerValue.restype = ByteArrayHandle
    lib.deserializeSerValue.argtypes = [POINTER(c_uint8), ffi_size_t, SerValueHandle]
    lib.deserializeSerValue.restype = c_bool_t
    lib.deserializeSerValueFromByteArray.argtypes = [ByteArrayHandle, SerValueHandle]
    lib.deserializeSerValueFromByteArray.restype = c_bool_t

    # Logger interface
    lib.getOrCreateLogger.argtypes = [c_char_p]
    lib.getOrCreateLogger.restype = LoggerHandle
    lib.setLoggerLevel.argtypes = [LoggerHandle, c_int32]
    lib.setLoggerLevel.restype = None
    lib.loggerTrace.argtypes = [LoggerHandle, c_char_p]
    lib.loggerTrace.restype = None
    lib.loggerDebug.argtypes = [LoggerHandle, c_char_p]
    lib.loggerDebug.restype = None
    lib.loggerInfo.argtypes = [LoggerHandle, c_char_p]
    lib.loggerInfo.restype = None
    lib.loggerWarn.argtypes = [LoggerHandle, c_char_p]
    lib.loggerWarn.restype = None
    lib.loggerError.argtypes = [LoggerHandle, c_char_p]
    lib.loggerError.restype = None
    lib.loggerCritical.argtypes = [LoggerHandle, c_char_p]
    lib.loggerCritical.restype = None

    if hasattr(lib, "luxonEnableCustomLogSink"):
        lib.luxonEnableCustomLogSink.argtypes = [c_bool_t]
        lib.luxonEnableCustomLogSink.restype = None

    # Peer interface
    lib.peerIsAuthenticated.argtypes = [PeerHandle]
    lib.peerIsAuthenticated.restype = c_bool_t
    lib.peerGetTransportProtocol.argtypes = [PeerHandle]
    lib.peerGetTransportProtocol.restype = LuxonServerProtocol
    lib.peerSend.argtypes = [PeerHandle, POINTER(c_uint8), ffi_size_t, c_uint8, LuxonDeliveryMode]
    lib.peerSend.restype = None
    lib.peerDisconnect.argtypes = [PeerHandle]
    lib.peerDisconnect.restype = None
    lib.peerGetUserId.argtypes = [PeerHandle]
    lib.peerGetUserId.restype = c_char_p
    lib.peerGetCurrentGame.argtypes = [PeerHandle]
    lib.peerGetCurrentGame.restype = GameHandle

    # Game peer interface
    lib.createGamePeerContainer.argtypes = []
    lib.createGamePeerContainer.restype = GamePeerHandle
    lib.destroyGamePeerContainer.argtypes = [GamePeerHandle]
    lib.destroyGamePeerContainer.restype = None
    lib.gamePeerIsValid.argtypes = [GamePeerHandle]
    lib.gamePeerIsValid.restype = c_bool_t
    lib.gamePeerGetActorId.argtypes = [GamePeerHandle]
    lib.gamePeerGetActorId.restype = c_int32
    lib.gamePeerHasInterestGroup.argtypes = [GamePeerHandle, c_uint8]
    lib.gamePeerHasInterestGroup.restype = c_bool_t
    lib.gamePeerSetInterestGroup.argtypes = [GamePeerHandle, c_uint8, c_bool_t]
    lib.gamePeerSetInterestGroup.restype = None
    lib.gamePeerGetInterestGroupsMask.argtypes = [GamePeerHandle, POINTER(c_uint32), ffi_size_t]
    lib.gamePeerGetInterestGroupsMask.restype = None
    lib.gamePeerSetInterestGroupsMask.argtypes = [GamePeerHandle, POINTER(c_uint32), ffi_size_t]
    lib.gamePeerSetInterestGroupsMask.restype = None
    lib.gamePeerGetActorProps.argtypes = [GamePeerHandle, SerValueHandle]
    lib.gamePeerGetActorProps.restype = None
    lib.gamePeerSetActorProps.argtypes = [GamePeerHandle, SerValueHandle]
    lib.gamePeerSetActorProps.restype = None
    lib.gamePeerGetBasePeer.argtypes = [GamePeerHandle]
    lib.gamePeerGetBasePeer.restype = PeerHandle
    lib.gamePeerDisconnect.argtypes = [GamePeerHandle]
    lib.gamePeerDisconnect.restype = c_bool_t

    # Event interface
    lib.createEvent.argtypes = []
    lib.createEvent.restype = EventHandle
    lib.destroyEvent.argtypes = [EventHandle]
    lib.destroyEvent.restype = None
    lib.eventSetRoutingMetadata.argtypes = [EventHandle, c_uint8, c_int32, LuxonDeliveryMode, c_uint8, c_uint8]
    lib.eventSetRoutingMetadata.restype = None
    lib.eventGetRoutingMetadata.argtypes = [EventHandle, POINTER(c_uint8), POINTER(c_int32), POINTER(LuxonDeliveryMode), POINTER(c_uint8), POINTER(c_uint8)]
    lib.eventGetRoutingMetadata.restype = None
    lib.eventSetReceiversAll.argtypes = [EventHandle]
    lib.eventSetReceiversAll.restype = None
    lib.eventSetReceiversGroup.argtypes = [EventHandle, c_uint8]
    lib.eventSetReceiversGroup.restype = None
    lib.eventSetReceiversActors.argtypes = [EventHandle, POINTER(c_int32), ffi_size_t]
    lib.eventSetReceiversActors.restype = None
    lib.eventGetReceiversType.argtypes = [EventHandle]
    lib.eventGetReceiversType.restype = LuxonEventReceiversType
    lib.eventGetReceiversGroup.argtypes = [EventHandle]
    lib.eventGetReceiversGroup.restype = c_uint8
    lib.eventGetReceiversActors.argtypes = [EventHandle, POINTER(c_int32), ffi_size_t, POINTER(ffi_size_t)]
    lib.eventGetReceiversActors.restype = None
    lib.eventSetData.argtypes = [EventHandle, SerValueHandle]
    lib.eventSetData.restype = None
    lib.eventGetData.argtypes = [EventHandle, SerValueHandle]
    lib.eventGetData.restype = None
    lib.eventSetTopParams.argtypes = [EventHandle, SerValueHandle]
    lib.eventSetTopParams.restype = None
    lib.eventGetTopParams.argtypes = [EventHandle, SerValueHandle]
    lib.eventGetTopParams.restype = None

    # Game interface
    lib.gameGetId.argtypes = [GameHandle, c_char_p, ffi_size_t]
    lib.gameGetId.restype = None
    lib.gameGetConfigState.argtypes = [GameHandle, POINTER(c_uint8), POINTER(c_bool_t), POINTER(c_bool_t), POINTER(c_bool_t), POINTER(c_uint8), POINTER(c_int32)]
    lib.gameGetConfigState.restype = None
    lib.gameSetConfigState.argtypes = [GameHandle, c_uint8, c_bool_t, c_bool_t, c_uint8, c_int32]
    lib.gameSetConfigState.restype = None
    lib.gameGetMasterActor.argtypes = [GameHandle]
    lib.gameGetMasterActor.restype = c_int32
    lib.gameSetMasterActor.argtypes = [GameHandle, c_int32]
    lib.gameSetMasterActor.restype = None
    lib.gameGetLastActorId.argtypes = [GameHandle]
    lib.gameGetLastActorId.restype = c_int32
    lib.gameGetMaxPeers.argtypes = [GameHandle]
    lib.gameGetMaxPeers.restype = c_uint8
    lib.gameSetMaxPeers.argtypes = [GameHandle, c_uint8]
    lib.gameSetMaxPeers.restype = None
    lib.gameGetTtlConfig.argtypes = [GameHandle, POINTER(c_int32), POINTER(c_int32)]
    lib.gameGetTtlConfig.restype = None
    lib.gameSetTtlConfig.argtypes = [GameHandle, c_int32, c_int32]
    lib.gameSetTtlConfig.restype = None
    lib.gameGetCustomProps.argtypes = [GameHandle, SerValueHandle]
    lib.gameGetCustomProps.restype = None
    lib.gameSetCustomProps.argtypes = [GameHandle, SerValueHandle]
    lib.gameSetCustomProps.restype = None
    lib.gameGetLobbyPropsToSerValue.argtypes = [GameHandle, SerValueHandle]
    lib.gameGetLobbyPropsToSerValue.restype = None
    lib.gameSetLobbyPropsFromSerValue.argtypes = [GameHandle, SerValueHandle]
    lib.gameSetLobbyPropsFromSerValue.restype = None
    lib.gameGetExpectedUsersToSerValue.argtypes = [GameHandle, SerValueHandle]
    lib.gameGetExpectedUsersToSerValue.restype = None
    lib.gameSetExpectedUsersFromSerValue.argtypes = [GameHandle, SerValueHandle]
    lib.gameSetExpectedUsersFromSerValue.restype = None
    lib.gameAddExpectedUser.argtypes = [GameHandle, c_char_p]
    lib.gameAddExpectedUser.restype = None
    lib.gameRemoveExpectedUser.argtypes = [GameHandle, c_char_p]
    lib.gameRemoveExpectedUser.restype = None
    lib.gameGetPeerCount.argtypes = [GameHandle]
    lib.gameGetPeerCount.restype = ffi_size_t
    lib.gameCreatePeer.argtypes = [GameHandle, PeerHandle]
    lib.gameCreatePeer.restype = GamePeerHandle
    lib.gameAddPeer.argtypes = [GameHandle, GamePeerHandle]
    lib.gameAddPeer.restype = GamePeerHandle
    lib.gameRemovePeer.argtypes = [GameHandle, PeerHandle]
    lib.gameRemovePeer.restype = c_bool_t
    lib.gameFloodPeer.argtypes = [GameHandle, GamePeerHandle]
    lib.gameFloodPeer.restype = c_bool_t
    lib.gameFindPeerByActorId.argtypes = [GameHandle, c_int32]
    lib.gameFindPeerByActorId.restype = GamePeerHandle
    lib.gameFindPeerByBasePeer.argtypes = [GameHandle, PeerHandle]
    lib.gameFindPeerByBasePeer.restype = GamePeerHandle
    lib.gameBroadcastEvent.argtypes = [GameHandle, EventHandle]
    lib.gameBroadcastEvent.restype = None
    lib.gameValidateJoin.argtypes = [GameHandle, c_char_p, ffi_size_t, POINTER(c_int16), c_char_p, ffi_size_t]
    lib.gameValidateJoin.restype = c_bool_t
    lib.gameTriggerLobbyUpdate.argtypes = [GameHandle]
    lib.gameTriggerLobbyUpdate.restype = None
    lib.gameGetGameProp.argtypes = [GameHandle, SerValueHandle, SerValueHandle]
    lib.gameGetGameProp.restype = None
    lib.gameGetLobbyGameProps.argtypes = [GameHandle, SerValueHandle]
    lib.gameGetLobbyGameProps.restype = None
    lib.gameGetGameProps.argtypes = [GameHandle, c_bool_t, SerValueHandle]
    lib.gameGetGameProps.restype = None
    lib.gameGetActorProps.argtypes = [GameHandle, SerValueHandle]
    lib.gameGetActorProps.restype = None
    lib.gameInsertGameProps.argtypes = [GameHandle, SerValueHandle]
    lib.gameInsertGameProps.restype = None
    lib.gameExpectGameProps.argtypes = [GameHandle, SerValueHandle]
    lib.gameExpectGameProps.restype = c_bool_t
    lib.gameInsertActorProps.argtypes = [GameHandle, c_int32, SerValueHandle]
    lib.gameInsertActorProps.restype = c_bool_t
    lib.gameExpectActorProps.argtypes = [GameHandle, c_int32, SerValueHandle]
    lib.gameExpectActorProps.restype = c_bool_t
    lib.gameMatchesFilter.argtypes = [SerValueHandle, SerValueHandle]
    lib.gameMatchesFilter.restype = c_bool_t

    # Lobby interface
    lib.createLobby.argtypes = [c_char_p, c_uint8]
    lib.createLobby.restype = LobbyHandle
    lib.destroyLobby.argtypes = [LobbyHandle]
    lib.destroyLobby.restype = None
    lib.lobbyCreateGame.argtypes = [LobbyHandle, c_char_p, c_bool_t, SerMessageHandle]
    lib.lobbyCreateGame.restype = GameHandle
    lib.lobbyGetPeerCount.argtypes = [LobbyHandle]
    lib.lobbyGetPeerCount.restype = ffi_size_t
    lib.lobbyGetMasterPeerCount.argtypes = [LobbyHandle]
    lib.lobbyGetMasterPeerCount.restype = ffi_size_t
    lib.lobbyQueryLobbies.argtypes = [LobbyHandle, c_char_p, c_char_p, ffi_size_t, POINTER(ffi_size_t)]
    lib.lobbyQueryLobbies.restype = c_bool_t

    # Server Manager config builder interface
    lib.createServerManagerConfig.argtypes = []
    lib.createServerManagerConfig.restype = ServerManagerConfigHandle
    lib.destroyServerManagerConfig.argtypes = [ServerManagerConfigHandle]
    lib.destroyServerManagerConfig.restype = None
    lib.loadServerManagerConfigFromFile.argtypes = [c_char_p]
    lib.loadServerManagerConfigFromFile.restype = ServerManagerConfigHandle
    lib.parseServerManagerConfig.argtypes = [c_char_p]
    lib.parseServerManagerConfig.restype = ServerManagerConfigHandle
    lib.serverManagerConfigAddServer.argtypes = [ServerManagerConfigHandle, LuxonServerType, c_uint16]
    lib.serverManagerConfigAddServer.restype = None
    lib.serverManagerConfigAddServerWithUdp.argtypes = [ServerManagerConfigHandle, LuxonServerType, c_uint16, c_char_p]
    lib.serverManagerConfigAddServerWithUdp.restype = None
    lib.serverManagerConfigAddEndpoint.argtypes = [ServerManagerConfigHandle, LuxonServerType, LuxonServerProtocol, c_char_p]
    lib.serverManagerConfigAddEndpoint.restype = None
    lib.serverManagerConfigSetLimits.argtypes = [ServerManagerConfigHandle, c_bool_t, c_uint, c_uint, c_uint32]
    lib.serverManagerConfigSetLimits.restype = None
    lib.serverManagerConfigSetSettingsDatabasePath.argtypes = [ServerManagerConfigHandle, c_char_p]
    lib.serverManagerConfigSetSettingsDatabasePath.restype = None
    lib.serverManagerConfigEnableHttp.argtypes = [ServerManagerConfigHandle, c_char_p, c_uint16]
    lib.serverManagerConfigEnableHttp.restype = None
    lib.serverManagerConfigDisableHttp.argtypes = [ServerManagerConfigHandle]
    lib.serverManagerConfigDisableHttp.restype = None

    # Server Manager interface
    lib.createServerManagerFromConfig.argtypes = [ServerManagerConfigHandle]
    lib.createServerManagerFromConfig.restype = ServerManagerHandle
    lib.createServerManagerFromFile.argtypes = [c_char_p]
    lib.createServerManagerFromFile.restype = ServerManagerHandle
    lib.createServerManagerFromContents.argtypes = [c_char_p]
    lib.createServerManagerFromContents.restype = ServerManagerHandle
    lib.destroyServerManager.argtypes = [ServerManagerHandle]
    lib.destroyServerManager.restype = None
    lib.serverManagerRun.argtypes = [ServerManagerHandle]
    lib.serverManagerRun.restype = None
    lib.serverManagerRunOnce.argtypes = [ServerManagerHandle]
    lib.serverManagerRunOnce.restype = c_bool_t
    lib.serverManagerStop.argtypes = [ServerManagerHandle]
    lib.serverManagerStop.restype = None
    lib.serverManagerGetEndpointOf.argtypes = [ServerManagerHandle, LuxonServerType, LuxonServerProtocol, c_char_p, ffi_size_t]
    lib.serverManagerGetEndpointOf.restype = c_bool_t
    lib.serverManagerGetConnectionCount.argtypes = [ServerManagerHandle]
    lib.serverManagerGetConnectionCount.restype = ffi_size_t
    lib.serverManagerGetMaxConnections.argtypes = [ServerManagerHandle]
    lib.serverManagerGetMaxConnections.restype = c_uint
    lib.serverManagerGetMaxGamePeers.argtypes = [ServerManagerHandle]
    lib.serverManagerGetMaxGamePeers.restype = c_uint8

    # Plugins / Hookpoints registration (dynamically bind)
    if hasattr(lib, "registerGamePlugin"):
        lib.registerGamePlugin.argtypes = [c_char_p]
        lib.registerGamePlugin.restype = c_uint32

    if hasattr(lib, "registerAuthPlugin"):
        lib.registerAuthPlugin.argtypes = [c_uint]
        lib.registerAuthPlugin.restype = c_bool_t

    if hasattr(lib, "registerHookpoints"):
        lib.registerHookpoints.argtypes = [ServerManagerHandle]
        lib.registerHookpoints.restype = None

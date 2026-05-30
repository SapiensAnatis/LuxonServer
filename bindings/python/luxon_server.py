from __future__ import annotations

import contextlib
import ctypes
import ctypes.util
import logging
import os
import sys
import warnings
import weakref

from dataclasses import dataclass
from enum import IntEnum
from types import TracebackType
from typing import Any, Callable, ClassVar, Iterator, Sequence

import luxon_server_ffi as ffi

__all__ = [
    "ffi",
    "load",
    "from_cdll",
    "LuxonServer",
    "Luxon",
    "ServerImports",
    "LibraryFeatures",
    "ServerType",
    "Protocol",
    "DeliveryMode",
    "EventReceiversType",
    "TTLConfig",
    "EventRoutingMetadataRaw",
    "JoinValidationResult",
    "BoolRef",
    "LuxonError",
    "FeatureUnavailableError",
    "ResourceClosedError",
    "NullHandleError",
    "ByteArray",
    "SerValue",
    "SerMessage",
    "Logger",
    "HandlerBase",
    "Peer",
    "GamePeer",
    "Event",
    "Game",
    "Lobby",
    "ServerManagerConfig",
    "ServerManager",
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_TEXT_ENCODING = "utf-8"
BufferLike = bytes | bytearray | memoryview | Sequence[int]


def _encode_text(value: str | bytes | os.PathLike[str] | os.PathLike[bytes] | None) -> bytes | None:
    if value is None:
        return None
    if isinstance(value, os.PathLike):
        value = os.fspath(value)
    if isinstance(value, bytes):
        return value
    return str(value).encode(_TEXT_ENCODING)


def _decode_text(value: bytes | str | None) -> str | None:
    if value is None:
        return None
    if isinstance(value, str):
        return value
    return value.decode(_TEXT_ENCODING, errors="replace")


def _decode_c_buffer(buf: Any, length: int | None = None) -> str:
    raw = buf.raw if hasattr(buf, "raw") else bytes(buf)
    if length is not None and length > 0:
        raw = raw[:length]
    return raw.split(b"\x00", 1)[0].decode(_TEXT_ENCODING, errors="replace")


def _normalize_handle(handle: Any) -> int:
    if handle is None:
        return 0
    if isinstance(handle, int):
        return handle
    if hasattr(handle, "value"):
        return int(handle.value or 0)
    return int(handle)


def _handle_of(value: Any) -> int:
    if isinstance(value, _NativeHandle):
        return value.handle
    return _normalize_handle(value)


def _as_u8_array(data: BufferLike) -> tuple[Any, int]:
    raw = data.tobytes() if isinstance(data, memoryview) else bytes(data)
    if not raw:
        return ctypes.POINTER(ctypes.c_uint8)(), 0
    arr = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
    return arr, len(raw)


def _as_int32_array(values: Sequence[int]) -> tuple[Any, int]:
    if not values:
        return ctypes.POINTER(ctypes.c_int32)(), 0
    arr = (ctypes.c_int32 * len(values))(*[int(v) for v in values])
    return arr, len(values)


def _as_uint32_array(values: Sequence[int]) -> tuple[Any, int]:
    if not values:
        return ctypes.POINTER(ctypes.c_uint32)(), 0
    arr = (ctypes.c_uint32 * len(values))(*[int(v) for v in values])
    return arr, len(values)


def _enum_or_int(enum_type: type[IntEnum], value: int) -> IntEnum | int:
    ivalue = int(value)
    try:
        return enum_type(ivalue)
    except ValueError:
        return ivalue


def _native_log_level_to_python(level: int) -> int:
    return {
        0: logging.DEBUG,     # trace
        1: logging.DEBUG,     # debug
        2: logging.INFO,      # info
        3: logging.WARNING,   # warn
        4: logging.ERROR,     # error
        5: logging.CRITICAL,  # critical
    }.get(int(level), logging.INFO)


# ---------------------------------------------------------------------------
# Exceptions
# ---------------------------------------------------------------------------

class LuxonError(RuntimeError):
    def __init__(self, message: str, *, error_type: str | None = None) -> None:
        super().__init__(message)
        self.error_type = error_type

    def __str__(self) -> str:
        base = super().__str__()
        return f"{self.error_type}: {base}" if self.error_type else base


class FeatureUnavailableError(LuxonError):
    pass


class ResourceClosedError(LuxonError):
    pass


class NullHandleError(LuxonError):
    pass


# ---------------------------------------------------------------------------
# Public enums / dataclasses
# ---------------------------------------------------------------------------

class ServerType(IntEnum):
    NONE = ffi.LUXON_SERVER_TYPE_NONE
    NAMESERVER = ffi.LUXON_SERVER_TYPE_NAMESERVER
    MASTERSERVER = ffi.LUXON_SERVER_TYPE_MASTERSERVER
    GAMESERVER = ffi.LUXON_SERVER_TYPE_GAMESERVER


class Protocol(IntEnum):
    UDP = ffi.LUXON_PROTOCOL_UDP
    TCP = ffi.LUXON_PROTOCOL_TCP
    WEBSOCKET = ffi.LUXON_PROTOCOL_WEBSOCKET


class DeliveryMode(IntEnum):
    UNRELIABLE = ffi.LUXON_DELIVERY_UNRELIABLE
    RELIABLE = ffi.LUXON_DELIVERY_RELIABLE
    UNSEQUENCED = ffi.LUXON_DELIVERY_UNSEQUENCED


class EventReceiversType(IntEnum):
    ALL = ffi.LUXON_RECEIVERS_ALL
    GROUP = ffi.LUXON_RECEIVERS_GROUP
    ACTORS = ffi.LUXON_RECEIVERS_ACTORS


@dataclass(slots=True, frozen=True)
class LibraryFeatures:
    set_server_imports: bool
    game_plugin: bool
    auth_plugin: bool
    hookpoints: bool
    custom_log_sink: bool


@dataclass(slots=True, frozen=True)
class TTLConfig:
    player_ttl: int
    empty_room_ttl: int


@dataclass(slots=True, frozen=True)
class EventRoutingMetadataRaw:
    code: int
    actor_id: int
    delivery_mode: DeliveryMode | int
    byte_a: int
    byte_b: int


@dataclass(slots=True, frozen=True)
class JoinValidationResult:
    accepted: bool
    error_code: int
    message: str


class BoolRef:
    def __init__(self, ptr: Any) -> None:
        self._ptr = ptr

    def _ensure(self) -> Any:
        if not self._ptr:
            raise ValueError("Null bool pointer")
        return self._ptr

    @property
    def value(self) -> bool:
        ptr = self._ensure()
        return bool(ptr[0])

    @value.setter
    def value(self, new_value: bool) -> None:
        ptr = self._ensure()
        ptr[0] = 1 if new_value else 0

    def __bool__(self) -> bool:
        return self.value

    def __repr__(self) -> str:
        try:
            return f"BoolRef({self.value})"
        except Exception:
            return "BoolRef(<null>)"


# ---------------------------------------------------------------------------
# Base native handle wrapper
# ---------------------------------------------------------------------------

class _NativeHandle:
    _destroy_func_name: ClassVar[str | None] = None

    def __init__(self, api: "LuxonServer", handle: Any, *, owned: bool = False) -> None:
        self._api = api
        self._handle = _normalize_handle(handle)
        if not self._handle:
            raise NullHandleError(f"{self.__class__.__name__} received a null native handle")

        self._closed = False
        self._owned = bool(owned and self._destroy_func_name)
        self._finalizer: weakref.finalize | None = None

        if self._owned:
            destroy = getattr(api.lib, self._destroy_func_name)  # type: ignore[arg-type]
            self._finalizer = weakref.finalize(
                self,
                type(self)._finalize_handle,
                destroy,
                self._handle,
                api,
            )

    @staticmethod
    def _finalize_handle(destroy_func: Callable[[Any], Any], handle: int, api: "LuxonServer") -> None:
        try:
            destroy_func(handle)
            api._raise_last_error_if_any()
        except Exception:
            pass

    @property
    def api(self) -> "LuxonServer":
        return self._api

    @property
    def handle(self) -> int:
        if self._closed:
            raise ResourceClosedError(f"{self.__class__.__name__} is closed")
        return self._handle

    @property
    def raw_handle(self) -> int:
        return self._handle

    @property
    def closed(self) -> bool:
        return self._closed

    @property
    def owned(self) -> bool:
        return self._owned

    def _disown(self) -> None:
        if self._finalizer and self._finalizer.alive:
            self._finalizer.detach()
        self._owned = False

    def detach(self) -> int:
        self._disown()
        return self._handle

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self._finalizer and self._finalizer.alive:
            self._finalizer()

    def __enter__(self) -> "Self":
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        self.close()

    def __int__(self) -> int:
        return self.handle

    def __repr__(self) -> str:
        state = "closed" if self._closed else "open"
        return f"<{self.__class__.__name__} handle=0x{self._handle:x} owned={self._owned} {state}>"


# ---------------------------------------------------------------------------
# Resource wrappers
# ---------------------------------------------------------------------------

class ByteArray(_NativeHandle):
    _destroy_func_name = "destroyByteArray"

    def __len__(self) -> int:
        return int(self._api._call("getByteArraySize", self.handle))

    def __bytes__(self) -> bytes:
        return self.to_bytes()

    def __iter__(self):
        return iter(self.to_bytes())

    def __getitem__(self, index: int | slice) -> int | bytes:
        if isinstance(index, slice):
            return self.to_bytes()[index]
        size = len(self)
        if index < 0:
            index += size
        if not (0 <= index < size):
            raise IndexError(index)
        return int(self._api._call("getInByteArray", self.handle, index))

    def __setitem__(self, index: int, value: int) -> None:
        size = len(self)
        if index < 0:
            index += size
        if not (0 <= index < size):
            raise IndexError(index)
        self._api._call("setInByteArray", self.handle, index, int(value))

    @property
    def data_ptr(self) -> int:
        return int(self._api._call("getByteArrayDataPtr", self.handle) or 0)

    def to_bytes(self) -> bytes:
        size = len(self)
        if size == 0:
            return b""
        out = (ctypes.c_uint8 * size)()
        self._api._call("copyToByteArray", self.handle, out, size)
        return bytes(out)

    def clone(self) -> "ByteArray":
        return self._api.create_byte_array(self)

    def copy_from(self, data: "ByteArray | BufferLike") -> int:
        if isinstance(data, ByteArray):
            data = data.to_bytes()
        buf, size = _as_u8_array(data)
        copied = self._api._call("copyFromByteArray", self.handle, buf, size)
        return int(copied)

    replace = copy_from

    def append(self, data: "ByteArray | BufferLike") -> "ByteArray":
        if isinstance(data, ByteArray):
            data = data.to_bytes()
        buf, size = _as_u8_array(data)
        self._api._call("appendToByteArray", self.handle, buf, size)
        return self

    extend = append

    def copy_to(self, size: int | None = None) -> bytes:
        actual = len(self)
        if size is not None:
            actual = min(actual, int(size))
        if actual <= 0:
            return b""
        out = (ctypes.c_uint8 * actual)()
        self._api._call("copyToByteArray", self.handle, out, actual)
        return bytes(out)

    def push(self, value: int) -> "ByteArray":
        self._api._call("pushToByteArray", self.handle, int(value))
        return self


class SerValue(_NativeHandle):
    _destroy_func_name = "destroySerValue"

    def __bytes__(self) -> bytes:
        return self.serialize()

    def clone(self) -> "SerValue":
        return self._api.create_ser_value(self)

    def serialize_to_byte_array(self) -> ByteArray:
        handle = self._api._require_non_null(
            self._api._call("serializeSerValue", self.handle),
            "serializeSerValue",
        )
        return ByteArray(self._api, handle, owned=True)

    def serialize(self) -> bytes:
        with self.serialize_to_byte_array() as out:
            return out.to_bytes()

    def deserialize(self, data: "SerValue | ByteArray | BufferLike") -> bool:
        if isinstance(data, SerValue):
            data = data.serialize()
        if isinstance(data, ByteArray):
            return bool(self._api._call("deserializeSerValueFromByteArray", data.handle, self.handle))
        buf, size = _as_u8_array(data)
        return bool(self._api._call("deserializeSerValue", buf, size, self.handle))


class SerMessage(_NativeHandle):
    _destroy_func_name = "destroySerMessage"

    def __bytes__(self) -> bytes:
        return self.serialize()

    def clone(self) -> "SerMessage":
        return self._api.create_ser_message(self)

    def serialize_to_byte_array(self) -> ByteArray:
        handle = self._api._require_non_null(
            self._api._call("serializeSerMessage", self.handle),
            "serializeSerMessage",
        )
        return ByteArray(self._api, handle, owned=True)

    def serialize(self) -> bytes:
        with self.serialize_to_byte_array() as out:
            return out.to_bytes()

    def deserialize(self, data: "SerMessage | ByteArray | BufferLike") -> bool:
        if isinstance(data, SerMessage):
            data = data.serialize()
        if isinstance(data, ByteArray):
            return bool(self._api._call("deserializeSerMessageFromByteArray", data.handle, self.handle))
        buf, size = _as_u8_array(data)
        return bool(self._api._call("deserializeSerMessage", buf, size, self.handle))


class Logger(_NativeHandle):
    _destroy_func_name = None

    def __init__(self, api: "LuxonServer", handle: Any, *, name: str | None = None) -> None:
        super().__init__(api, handle, owned=False)
        self.name = name

    def __repr__(self) -> str:
        return f"<Logger name={self.name!r} handle=0x{self.raw_handle:x}>"

    def set_level(self, level: int) -> "Logger":
        self._api._call("setLoggerLevel", self.handle, int(level))
        return self

    def _write(self, method: str, message: str | bytes) -> None:
        self._api._call(method, self.handle, _encode_text(message))

    def trace(self, message: str | bytes) -> None:
        self._write("loggerTrace", message)

    def debug(self, message: str | bytes) -> None:
        self._write("loggerDebug", message)

    def info(self, message: str | bytes) -> None:
        self._write("loggerInfo", message)

    def warn(self, message: str | bytes) -> None:
        self._write("loggerWarn", message)

    warning = warn

    def error(self, message: str | bytes) -> None:
        self._write("loggerError", message)

    def critical(self, message: str | bytes) -> None:
        self._write("loggerCritical", message)

    def log(self, level: int, message: str | bytes) -> None:
        raw = int(level)
        if raw == 0:
            self.trace(message)
        elif raw == 1:
            self.debug(message)
        elif raw == 2:
            self.info(message)
        elif raw == 3:
            self.warn(message)
        elif raw == 4:
            self.error(message)
        elif raw == 5:
            self.critical(message)
        elif raw >= logging.CRITICAL:
            self.critical(message)
        elif raw >= logging.ERROR:
            self.error(message)
        elif raw >= logging.WARNING:
            self.warn(message)
        elif raw >= logging.INFO:
            self.info(message)
        else:
            self.debug(message)


class HandlerBase(_NativeHandle):
    _destroy_func_name = None


class Peer(_NativeHandle):
    _destroy_func_name = None

    @property
    def authenticated(self) -> bool:
        return bool(self._api._call("peerIsAuthenticated", self.handle))

    @property
    def transport_protocol(self) -> Protocol | int:
        raw = int(self._api._call("peerGetTransportProtocol", self.handle))
        return _enum_or_int(Protocol, raw)

    @property
    def user_id(self) -> str | None:
        return _decode_text(self._api._call("peerGetUserId", self.handle))

    @property
    def current_game(self) -> "Game | None":
        handle = self._api._call("peerGetCurrentGame", self.handle)
        return Game(self._api, handle, owned=False) if _normalize_handle(handle) else None

    def send(
        self,
        data: ByteArray | BufferLike,
        *,
        channel: int = 0,
        delivery: DeliveryMode | int = DeliveryMode.RELIABLE,
    ) -> None:
        if isinstance(data, ByteArray):
            raw = data.to_bytes()
            buf, size = _as_u8_array(raw)
        else:
            buf, size = _as_u8_array(data)
        self._api._call(
            "peerSend",
            self.handle,
            buf,
            size,
            int(channel),
            int(DeliveryMode(delivery)),
        )

    def disconnect(self) -> None:
        self._api._call("peerDisconnect", self.handle)


class GamePeer(_NativeHandle):
    _destroy_func_name = "destroyGamePeerContainer"

    @property
    def valid(self) -> bool:
        return bool(self._api._call("gamePeerIsValid", self.handle))

    @property
    def actor_id(self) -> int:
        return int(self._api._call("gamePeerGetActorId", self.handle))

    @property
    def base_peer(self) -> Peer | None:
        handle = self._api._call("gamePeerGetBasePeer", self.handle)
        return Peer(self._api, handle, owned=False) if _normalize_handle(handle) else None

    def has_interest_group(self, group: int) -> bool:
        return bool(self._api._call("gamePeerHasInterestGroup", self.handle, int(group)))

    def set_interest_group(self, group: int, enabled: bool) -> "GamePeer":
        self._api._call("gamePeerSetInterestGroup", self.handle, int(group), bool(enabled))
        return self

    def get_interest_groups_mask(self, words: int = 8) -> tuple[int, ...]:
        count = max(0, int(words))
        if count == 0:
            return ()
        out = (ctypes.c_uint32 * count)()
        self._api._call("gamePeerGetInterestGroupsMask", self.handle, out, count)
        return tuple(int(v) for v in out)

    def set_interest_groups_mask(self, mask: Sequence[int]) -> "GamePeer":
        arr, count = _as_uint32_array(mask)
        self._api._call("gamePeerSetInterestGroupsMask", self.handle, arr, count)
        return self

    def get_actor_props(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("gamePeerGetActorProps", self.handle, out.handle)
        return out

    def set_actor_props(self, value: SerValue | ByteArray | BufferLike) -> "GamePeer":
        with self._api._coerce_ser_value(value) as sv:
            self._api._call("gamePeerSetActorProps", self.handle, sv.handle)
        return self

    def disconnect(self) -> bool:
        return bool(self._api._call("gamePeerDisconnect", self.handle))


class Event(_NativeHandle):
    _destroy_func_name = "destroyEvent"

    @property
    def receivers_type(self) -> EventReceiversType | int:
        raw = int(self._api._call("eventGetReceiversType", self.handle))
        return _enum_or_int(EventReceiversType, raw)

    @property
    def receivers_group(self) -> int:
        return int(self._api._call("eventGetReceiversGroup", self.handle))

    def set_routing_metadata(
        self,
        code: int,
        actor_id: int,
        delivery_mode: DeliveryMode | int,
        byte_a: int,
        byte_b: int,
    ) -> "Event":
        self._api._call(
            "eventSetRoutingMetadata",
            self.handle,
            int(code),
            int(actor_id),
            int(DeliveryMode(delivery_mode)),
            int(byte_a),
            int(byte_b),
        )
        return self

    def get_routing_metadata(self) -> EventRoutingMetadataRaw:
        code = ctypes.c_uint8()
        actor_id = ctypes.c_int32()
        delivery = ffi.LuxonDeliveryMode()
        byte_a = ctypes.c_uint8()
        byte_b = ctypes.c_uint8()

        self._api._call(
            "eventGetRoutingMetadata",
            self.handle,
            ctypes.byref(code),
            ctypes.byref(actor_id),
            ctypes.byref(delivery),
            ctypes.byref(byte_a),
            ctypes.byref(byte_b),
        )
        return EventRoutingMetadataRaw(
            code=int(code.value),
            actor_id=int(actor_id.value),
            delivery_mode=_enum_or_int(DeliveryMode, int(delivery.value)),
            byte_a=int(byte_a.value),
            byte_b=int(byte_b.value),
        )

    def set_receivers_all(self) -> "Event":
        self._api._call("eventSetReceiversAll", self.handle)
        return self

    def set_receivers_group(self, group: int) -> "Event":
        self._api._call("eventSetReceiversGroup", self.handle, int(group))
        return self

    def set_receivers_actors(self, actor_ids: Sequence[int]) -> "Event":
        arr, count = _as_int32_array(actor_ids)
        self._api._call("eventSetReceiversActors", self.handle, arr, count)
        return self

    def get_receivers_actors(self, initial_capacity: int = 16) -> tuple[int, ...]:
        capacity = max(1, int(initial_capacity))
        while True:
            out = (ctypes.c_int32 * capacity)()
            needed = ffi.ffi_size_t(0)
            self._api._call(
                "eventGetReceiversActors",
                self.handle,
                out,
                capacity,
                ctypes.byref(needed),
            )
            need = int(needed.value)
            if need <= capacity:
                return tuple(int(out[i]) for i in range(need))
            capacity = need

    def set_data(self, value: SerValue | ByteArray | BufferLike) -> "Event":
        with self._api._coerce_ser_value(value) as sv:
            self._api._call("eventSetData", self.handle, sv.handle)
        return self

    def get_data(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("eventGetData", self.handle, out.handle)
        return out

    def set_top_params(self, value: SerValue | ByteArray | BufferLike) -> "Event":
        with self._api._coerce_ser_value(value) as sv:
            self._api._call("eventSetTopParams", self.handle, sv.handle)
        return self

    def get_top_params(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("eventGetTopParams", self.handle, out.handle)
        return out


class Game(_NativeHandle):
    _destroy_func_name = None

    @property
    def id(self) -> str:
        return self.get_id()

    @property
    def master_actor(self) -> int:
        return int(self._api._call("gameGetMasterActor", self.handle))

    @master_actor.setter
    def master_actor(self, actor_id: int) -> None:
        self._api._call("gameSetMasterActor", self.handle, int(actor_id))

    @property
    def last_actor_id(self) -> int:
        return int(self._api._call("gameGetLastActorId", self.handle))

    @property
    def max_peers(self) -> int:
        return int(self._api._call("gameGetMaxPeers", self.handle))

    @max_peers.setter
    def max_peers(self, value: int) -> None:
        self._api._call("gameSetMaxPeers", self.handle, int(value))

    @property
    def ttl_config(self) -> TTLConfig:
        player_ttl = ctypes.c_int32()
        empty_room_ttl = ctypes.c_int32()
        self._api._call(
            "gameGetTtlConfig",
            self.handle,
            ctypes.byref(player_ttl),
            ctypes.byref(empty_room_ttl),
        )
        return TTLConfig(
            player_ttl=int(player_ttl.value),
            empty_room_ttl=int(empty_room_ttl.value),
        )

    @ttl_config.setter
    def ttl_config(self, value: TTLConfig | tuple[int, int]) -> None:
        if not isinstance(value, TTLConfig):
            value = TTLConfig(int(value[0]), int(value[1]))
        self._api._call("gameSetTtlConfig", self.handle, value.player_ttl, value.empty_room_ttl)

    def get_id(self, max_bytes: int = 1024) -> str:
        size = max(1, int(max_bytes))
        buf = ctypes.create_string_buffer(size)
        self._api._call("gameGetId", self.handle, buf, size)
        if b"\x00" not in buf.raw:
            warnings.warn(
                "gameGetId() may have truncated the room id; increase max_bytes",
                RuntimeWarning,
                stacklevel=2,
            )
        return _decode_c_buffer(buf)

    def get_config_state_raw(self) -> tuple[int, bool, bool, bool, int, int]:
        byte_a = ctypes.c_uint8()
        flag_a = ffi.c_bool_t()
        flag_b = ffi.c_bool_t()
        flag_c = ffi.c_bool_t()
        byte_b = ctypes.c_uint8()
        int_a = ctypes.c_int32()

        self._api._call(
            "gameGetConfigState",
            self.handle,
            ctypes.byref(byte_a),
            ctypes.byref(flag_a),
            ctypes.byref(flag_b),
            ctypes.byref(flag_c),
            ctypes.byref(byte_b),
            ctypes.byref(int_a),
        )
        return (
            int(byte_a.value),
            bool(flag_a.value),
            bool(flag_b.value),
            bool(flag_c.value),
            int(byte_b.value),
            int(int_a.value),
        )

    def set_config_state_raw(
        self,
        byte_a: int,
        flag_a: bool,
        flag_b: bool,
        byte_b: int,
        int_a: int,
    ) -> "Game":
        self._api._call(
            "gameSetConfigState",
            self.handle,
            int(byte_a),
            bool(flag_a),
            bool(flag_b),
            int(byte_b),
            int(int_a),
        )
        return self

    def get_custom_props(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("gameGetCustomProps", self.handle, out.handle)
        return out

    def set_custom_props(self, value: SerValue | ByteArray | BufferLike) -> "Game":
        with self._api._coerce_ser_value(value) as sv:
            self._api._call("gameSetCustomProps", self.handle, sv.handle)
        return self

    def get_lobby_props(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("gameGetLobbyPropsToSerValue", self.handle, out.handle)
        return out

    def set_lobby_props(self, value: SerValue | ByteArray | BufferLike) -> "Game":
        with self._api._coerce_ser_value(value) as sv:
            self._api._call("gameSetLobbyPropsFromSerValue", self.handle, sv.handle)
        return self

    def get_expected_users(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("gameGetExpectedUsersToSerValue", self.handle, out.handle)
        return out

    def set_expected_users(self, value: SerValue | ByteArray | BufferLike) -> "Game":
        with self._api._coerce_ser_value(value) as sv:
            self._api._call("gameSetExpectedUsersFromSerValue", self.handle, sv.handle)
        return self

    def add_expected_user(self, user_id: str | bytes) -> "Game":
        self._api._call("gameAddExpectedUser", self.handle, _encode_text(user_id))
        return self

    def remove_expected_user(self, user_id: str | bytes) -> "Game":
        self._api._call("gameRemoveExpectedUser", self.handle, _encode_text(user_id))
        return self

    @property
    def peer_count(self) -> int:
        return int(self._api._call("gameGetPeerCount", self.handle))

    def create_peer(self, peer: Peer | int) -> GamePeer:
        handle = self._api._require_non_null(
            self._api._call("gameCreatePeer", self.handle, _handle_of(peer)),
            "gameCreatePeer",
        )
        return GamePeer(self._api, handle, owned=True)

    def add_peer(self, game_peer: GamePeer | int) -> GamePeer | None:
        input_handle = _handle_of(game_peer)
        result = self._api._call("gameAddPeer", self.handle, input_handle)
        result_handle = _normalize_handle(result)
        if not result_handle:
            return None
        if isinstance(game_peer, GamePeer) and result_handle == game_peer.raw_handle:
            game_peer._disown()
            return game_peer
        return GamePeer(self._api, result_handle, owned=False)

    def remove_peer(self, peer: Peer | int) -> bool:
        return bool(self._api._call("gameRemovePeer", self.handle, _handle_of(peer)))

    def flood_peer(self, game_peer: GamePeer | int) -> bool:
        return bool(self._api._call("gameFloodPeer", self.handle, _handle_of(game_peer)))

    def find_peer_by_actor_id(self, actor_id: int) -> GamePeer | None:
        handle = self._api._call("gameFindPeerByActorId", self.handle, int(actor_id))
        return GamePeer(self._api, handle, owned=False) if _normalize_handle(handle) else None

    def find_peer_by_base_peer(self, peer: Peer | int) -> GamePeer | None:
        handle = self._api._call("gameFindPeerByBasePeer", self.handle, _handle_of(peer))
        return GamePeer(self._api, handle, owned=False) if _normalize_handle(handle) else None

    def broadcast_event(self, event: Event | int) -> "Game":
        self._api._call("gameBroadcastEvent", self.handle, _handle_of(event))
        return self

    def validate_join(self, user_id: str | bytes, reason_capacity: int = 1024) -> JoinValidationResult:
        user_id_b = _encode_text(user_id) or b""
        err = ctypes.c_int16()
        cap = max(1, int(reason_capacity))
        reason = ctypes.create_string_buffer(cap)

        accepted = bool(
            self._api._call(
                "gameValidateJoin",
                self.handle,
                user_id_b,
                len(user_id_b),
                ctypes.byref(err),
                reason,
                cap,
            )
        )
        return JoinValidationResult(
            accepted=accepted,
            error_code=int(err.value),
            message=_decode_c_buffer(reason),
        )

    def trigger_lobby_update(self) -> "Game":
        self._api._call("gameTriggerLobbyUpdate", self.handle)
        return self

    def get_game_prop(self, key: SerValue | ByteArray | BufferLike) -> SerValue:
        with self._api._coerce_ser_value(key) as key_sv:
            out = self._api.create_ser_value()
            self._api._call("gameGetGameProp", self.handle, key_sv.handle, out.handle)
            return out

    def get_lobby_game_props(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("gameGetLobbyGameProps", self.handle, out.handle)
        return out

    def get_game_props(self, include_private: bool = False) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("gameGetGameProps", self.handle, bool(include_private), out.handle)
        return out

    def get_actor_props(self) -> SerValue:
        out = self._api.create_ser_value()
        self._api._call("gameGetActorProps", self.handle, out.handle)
        return out

    def insert_game_props(self, props: SerValue | ByteArray | BufferLike) -> "Game":
        with self._api._coerce_ser_value(props) as sv:
            self._api._call("gameInsertGameProps", self.handle, sv.handle)
        return self

    def expect_game_props(self, props: SerValue | ByteArray | BufferLike) -> bool:
        with self._api._coerce_ser_value(props) as sv:
            return bool(self._api._call("gameExpectGameProps", self.handle, sv.handle))

    def insert_actor_props(self, actor_id: int, props: SerValue | ByteArray | BufferLike) -> bool:
        with self._api._coerce_ser_value(props) as sv:
            return bool(self._api._call("gameInsertActorProps", self.handle, int(actor_id), sv.handle))

    def expect_actor_props(self, actor_id: int, props: SerValue | ByteArray | BufferLike) -> bool:
        with self._api._coerce_ser_value(props) as sv:
            return bool(self._api._call("gameExpectActorProps", self.handle, int(actor_id), sv.handle))

    def matches_filter(self, filter_value: SerValue | ByteArray | BufferLike, *, include_private: bool = False) -> bool:
        with self.get_game_props(include_private=include_private) as props:
            return self._api.matches_filter(filter_value, props)


class Lobby(_NativeHandle):
    _destroy_func_name = "destroyLobby"

    @property
    def peer_count(self) -> int:
        return int(self._api._call("lobbyGetPeerCount", self.handle))

    @property
    def master_peer_count(self) -> int:
        return int(self._api._call("lobbyGetMasterPeerCount", self.handle))

    def create_game(
        self,
        game_id: str | bytes,
        flag: bool = False,
        message: SerMessage | ByteArray | BufferLike | None = None,
    ) -> Game:
        with self._api._coerce_ser_message(message, empty_if_none=True) as msg:
            handle = self._api._require_non_null(
                self._api._call("lobbyCreateGame", self.handle, _encode_text(game_id), bool(flag), msg.handle),
                "lobbyCreateGame",
            )
            return Game(self._api, handle, owned=False)

    def query_lobbies(self, query: str | bytes | None = None, initial_capacity: int = 4096) -> str:
        query_b = _encode_text(query) or b""
        capacity = max(1, int(initial_capacity))

        while True:
            out = ctypes.create_string_buffer(capacity)
            needed = ffi.ffi_size_t(0)

            ok = bool(
                self._api._call(
                    "lobbyQueryLobbies",
                    self.handle,
                    query_b,
                    out,
                    capacity,
                    ctypes.byref(needed),
                )
            )
            required = int(needed.value)

            if required > capacity:
                capacity = required + 1
                continue

            if not ok and required == 0:
                return ""
            return _decode_c_buffer(out, required if required else None)


class ServerManagerConfig(_NativeHandle):
    _destroy_func_name = "destroyServerManagerConfig"

    def add_server(self, server_type: ServerType | int, port: int) -> "ServerManagerConfig":
        self._api._call("serverManagerConfigAddServer", self.handle, int(ServerType(server_type)), int(port))
        return self

    def add_server_with_udp(
        self,
        server_type: ServerType | int,
        port: int,
        udp_endpoint: str | bytes | None,
    ) -> "ServerManagerConfig":
        self._api._call(
            "serverManagerConfigAddServerWithUdp",
            self.handle,
            int(ServerType(server_type)),
            int(port),
            _encode_text(udp_endpoint),
        )
        return self

    def add_endpoint(
        self,
        server_type: ServerType | int,
        protocol: Protocol | int,
        endpoint: str | bytes,
    ) -> "ServerManagerConfig":
        self._api._call(
            "serverManagerConfigAddEndpoint",
            self.handle,
            int(ServerType(server_type)),
            int(Protocol(protocol)),
            _encode_text(endpoint),
        )
        return self

    def set_limits(self, enabled: bool, limit_a: int, limit_b: int, limit_c: int) -> "ServerManagerConfig":
        self._api._call(
            "serverManagerConfigSetLimits",
            self.handle,
            bool(enabled),
            int(limit_a),
            int(limit_b),
            int(limit_c),
        )
        return self

    def set_settings_database_path(self, path: str | bytes | os.PathLike[str] | os.PathLike[bytes]) -> "ServerManagerConfig":
        self._api._call("serverManagerConfigSetSettingsDatabasePath", self.handle, _encode_text(path))
        return self

    def enable_http(self, host: str | bytes | None, port: int) -> "ServerManagerConfig":
        self._api._call("serverManagerConfigEnableHttp", self.handle, _encode_text(host), int(port))
        return self

    def disable_http(self) -> "ServerManagerConfig":
        self._api._call("serverManagerConfigDisableHttp", self.handle)
        return self


class ServerManager(_NativeHandle):
    _destroy_func_name = "destroyServerManager"

    @property
    def connection_count(self) -> int:
        return int(self._api._call("serverManagerGetConnectionCount", self.handle))

    @property
    def max_connections(self) -> int:
        return int(self._api._call("serverManagerGetMaxConnections", self.handle))

    @property
    def max_game_peers(self) -> int:
        return int(self._api._call("serverManagerGetMaxGamePeers", self.handle))

    def run(self) -> None:
        self._api._call("serverManagerRun", self.handle)

    def run_once(self) -> bool:
        return bool(self._api._call("serverManagerRunOnce", self.handle))

    def stop(self) -> "ServerManager":
        self._api._call("serverManagerStop", self.handle)
        return self

    def get_endpoint_of(
        self,
        server_type: ServerType | int,
        protocol: Protocol | int,
        capacity: int = 512,
    ) -> str | None:
        size = max(1, int(capacity))
        out = ctypes.create_string_buffer(size)
        ok = bool(
            self._api._call(
                "serverManagerGetEndpointOf",
                self.handle,
                int(ServerType(server_type)),
                int(Protocol(protocol)),
                out,
                size,
            )
        )
        if not ok:
            return None
        if b"\x00" not in out.raw:
            warnings.warn(
                "serverManagerGetEndpointOf() may have truncated the endpoint; increase capacity",
                RuntimeWarning,
                stacklevel=2,
            )
        return _decode_c_buffer(out)

    def register_hookpoints(self) -> "ServerManager":
        self._api.register_hookpoints(self)
        return self

    def close(self) -> None:
        if self.closed:
            return
        try:
            self.stop()
        except Exception:
            pass
        super().close()


# ---------------------------------------------------------------------------
# Server imports / callback registry
# ---------------------------------------------------------------------------

class ServerImports:
    _VALID_CALLBACK_NAMES: ClassVar[set[str]] = {
        "gamePluginOnAttach",
        "gamePluginOnCreateGame",
        "gamePluginBeforeJoin",
        "gamePluginOnJoinGame",
        "gamePluginOnLeave",
        "gamePluginOnRaiseEvent",
        "gamePluginBeforeSetProperties",
        "gamePluginOnSetProperties",
        "gamePluginBeforeCloseGame",
        "gamePluginOnCloseGame",
        "authPluginOnAuthenticate",
        "customLogSink",
        "hookpointMasterServerHandleOperationRequestJoinGame",
        "hookpointMasterServerHandleOperationRequestCreateGame",
        "hookpointHandlerBaseHandleENetCommandOnMessage",
        "hookpointAppLoadAppSettings",
    }

    def __init__(self, *, warn_on_exception: bool = True) -> None:
        self.warn_on_exception = warn_on_exception
        self.last_callback_name: str | None = None
        self.last_callback_exception: BaseException | None = None
        self._user_callbacks: dict[str, Callable[..., Any]] = {}
        self._ffi_imports: ffi.LuxonServerImports | None = None
        self._ffi_callbacks: dict[str, Any] = {}
        self._api: LuxonServer | None = None

    def __repr__(self) -> str:
        names = ", ".join(sorted(self._user_callbacks))
        return f"<ServerImports callbacks=[{names}]>"

    @property
    def registered_callbacks(self) -> tuple[str, ...]:
        return tuple(sorted(self._user_callbacks))

    def set(self, name: str, fn: Callable[..., Any]) -> Callable[..., Any]:
        if name not in self._VALID_CALLBACK_NAMES:
            raise KeyError(f"Unknown Luxon callback: {name}")
        self._user_callbacks[name] = fn
        return fn

    def callback(self, name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
        def decorator(fn: Callable[..., Any]) -> Callable[..., Any]:
            return self.set(name, fn)
        return decorator

    def clear(self, name: str | None = None) -> None:
        if name is None:
            self._user_callbacks.clear()
            return
        self._user_callbacks.pop(name, None)

    # ergonomic decorators
    def game_plugin_on_attach(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginOnAttach", fn)

    def game_plugin_on_create_game(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginOnCreateGame", fn)

    def game_plugin_before_join(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginBeforeJoin", fn)

    def game_plugin_on_join_game(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginOnJoinGame", fn)

    def game_plugin_on_leave(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginOnLeave", fn)

    def game_plugin_on_raise_event(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginOnRaiseEvent", fn)

    def game_plugin_before_set_properties(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginBeforeSetProperties", fn)

    def game_plugin_on_set_properties(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginOnSetProperties", fn)

    def game_plugin_before_close_game(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginBeforeCloseGame", fn)

    def game_plugin_on_close_game(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("gamePluginOnCloseGame", fn)

    def auth_plugin_on_authenticate(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("authPluginOnAuthenticate", fn)

    def custom_log_sink(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("customLogSink", fn)

    def hookpoint_master_server_handle_operation_request_join_game(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("hookpointMasterServerHandleOperationRequestJoinGame", fn)

    def hookpoint_master_server_handle_operation_request_create_game(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("hookpointMasterServerHandleOperationRequestCreateGame", fn)

    def hookpoint_handler_base_handle_enet_command_on_message(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("hookpointHandlerBaseHandleENetCommandOnMessage", fn)

    def hookpoint_app_load_app_settings(self, fn: Callable[..., Any]) -> Callable[..., Any]:
        return self.set("hookpointAppLoadAppSettings", fn)

    def install(self, api: "LuxonServer") -> "ServerImports":
        api.set_server_imports(self)
        return self

    def _warn_for_unavailable_features(self, features: LibraryFeatures) -> None:
        for name in self._user_callbacks:
            if name.startswith("gamePlugin") and not features.game_plugin:
                warnings.warn(
                    f"Callback '{name}' was registered but the native library does not export registerGamePlugin",
                    RuntimeWarning,
                    stacklevel=3,
                )
            elif name.startswith("authPlugin") and not features.auth_plugin:
                warnings.warn(
                    f"Callback '{name}' was registered but the native library does not export registerAuthPlugin",
                    RuntimeWarning,
                    stacklevel=3,
                )
            elif name.startswith("hookpoint") and not features.hookpoints:
                warnings.warn(
                    f"Callback '{name}' was registered but the native library does not export registerHookpoints",
                    RuntimeWarning,
                    stacklevel=3,
                )
            elif name == "customLogSink" and not features.custom_log_sink:
                warnings.warn(
                    "Callback 'customLogSink' was registered but the native library does not export luxonEnableCustomLogSink",
                    RuntimeWarning,
                    stacklevel=3,
                )

    def _record_callback_exception(self, name: str, exc: BaseException) -> None:
        self.last_callback_name = name
        self.last_callback_exception = exc
        if self.warn_on_exception:
            warnings.warn(
                f"Unhandled exception in Luxon callback '{name}': {exc!r}",
                RuntimeWarning,
                stacklevel=3,
            )

    def build(self, api: "LuxonServer") -> ffi.LuxonServerImports:
        self._api = api
        imports = ffi.LuxonServerImports()
        callbacks: dict[str, Any] = {}

        def maybe(cls: type[_NativeHandle], handle: Any) -> _NativeHandle | None:
            return cls(api, handle, owned=False) if _normalize_handle(handle) else None

        def make_void(name: str, adapter: Callable[..., Any]) -> Callable[..., None]:
            def wrapped(*args: Any) -> None:
                cb = self._user_callbacks.get(name)
                if cb is None:
                    return None
                try:
                    adapter(cb, *args)
                except Exception as exc:
                    self._record_callback_exception(name, exc)
            return wrapped

        def make_bool(
            name: str,
            *,
            on_missing: bool,
            on_exception: bool,
            adapter: Callable[..., Any],
        ) -> Callable[..., int]:
            def wrapped(*args: Any) -> int:
                cb = self._user_callbacks.get(name)
                if cb is None:
                    return 1 if on_missing else 0
                try:
                    return 1 if bool(adapter(cb, *args)) else 0
                except Exception as exc:
                    self._record_callback_exception(name, exc)
                    return 1 if on_exception else 0
            return wrapped

        callbacks["gamePluginOnAttach"] = ffi.gamePluginOnAttach_t(
            make_void(
                "gamePluginOnAttach",
                lambda cb, plugin_id, game_h:
                    cb(int(plugin_id), maybe(Game, game_h)),
            )
        )

        callbacks["gamePluginOnCreateGame"] = ffi.gamePluginOnCreateGame_t(
            make_bool(
                "gamePluginOnCreateGame",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, msg_h, peer_h, flag_a, flag_b:
                    cb(
                        int(plugin_id),
                        maybe(Game, game_h),
                        maybe(SerMessage, msg_h),
                        maybe(Peer, peer_h),
                        bool(flag_a),
                        bool(flag_b),
                    ),
            )
        )

        callbacks["gamePluginBeforeJoin"] = ffi.gamePluginBeforeJoin_t(
            make_bool(
                "gamePluginBeforeJoin",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, msg_h, peer_h:
                    cb(
                        int(plugin_id),
                        maybe(Game, game_h),
                        maybe(SerMessage, msg_h),
                        maybe(Peer, peer_h),
                    ),
            )
        )

        callbacks["gamePluginOnJoinGame"] = ffi.gamePluginOnJoinGame_t(
            make_bool(
                "gamePluginOnJoinGame",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, msg_h, game_peer_h:
                    cb(
                        int(plugin_id),
                        maybe(Game, game_h),
                        maybe(SerMessage, msg_h),
                        maybe(GamePeer, game_peer_h),
                    ),
            )
        )

        callbacks["gamePluginOnLeave"] = ffi.gamePluginOnLeave_t(
            make_bool(
                "gamePluginOnLeave",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, msg_h, game_peer_h:
                    cb(
                        int(plugin_id),
                        maybe(Game, game_h),
                        maybe(SerMessage, msg_h),
                        maybe(GamePeer, game_peer_h),
                    ),
            )
        )

        callbacks["gamePluginOnRaiseEvent"] = ffi.gamePluginOnRaiseEvent_t(
            make_bool(
                "gamePluginOnRaiseEvent",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, msg_h, game_peer_h, event_h, byte_a:
                    cb(
                        int(plugin_id),
                        maybe(Game, game_h),
                        maybe(SerMessage, msg_h),
                        maybe(GamePeer, game_peer_h),
                        maybe(Event, event_h),
                        int(byte_a),
                    ),
            )
        )

        callbacks["gamePluginBeforeSetProperties"] = ffi.gamePluginBeforeSetProperties_t(
            make_bool(
                "gamePluginBeforeSetProperties",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, msg_h, game_peer_h, flag_a, actor_id, value_a_h, value_b_h:
                    cb(
                        int(plugin_id),
                        maybe(Game, game_h),
                        maybe(SerMessage, msg_h),
                        maybe(GamePeer, game_peer_h),
                        bool(flag_a),
                        int(actor_id),
                        maybe(SerValue, value_a_h),
                        maybe(SerValue, value_b_h),
                    ),
            )
        )

        callbacks["gamePluginOnSetProperties"] = ffi.gamePluginOnSetProperties_t(
            make_bool(
                "gamePluginOnSetProperties",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, msg_h, game_peer_h, flag_a, actor_id, value_a_h, value_b_h:
                    cb(
                        int(plugin_id),
                        maybe(Game, game_h),
                        maybe(SerMessage, msg_h),
                        maybe(GamePeer, game_peer_h),
                        bool(flag_a),
                        int(actor_id),
                        maybe(SerValue, value_a_h),
                        maybe(SerValue, value_b_h),
                    ),
            )
        )

        callbacks["gamePluginBeforeCloseGame"] = ffi.gamePluginBeforeCloseGame_t(
            make_bool(
                "gamePluginBeforeCloseGame",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, flag_a:
                    cb(int(plugin_id), maybe(Game, game_h), bool(flag_a)),
            )
        )

        callbacks["gamePluginOnCloseGame"] = ffi.gamePluginOnCloseGame_t(
            make_bool(
                "gamePluginOnCloseGame",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, plugin_id, game_h, flag_a:
                    cb(int(plugin_id), maybe(Game, game_h), bool(flag_a)),
            )
        )

        callbacks["authPluginOnAuthenticate"] = ffi.authPluginOnAuthenticate_t(
            make_bool(
                "authPluginOnAuthenticate",
                on_missing=False,
                on_exception=False,
                adapter=lambda cb, uint_a, s1, s2, s3, s4, s5, s6, size_a, msg_h:
                    cb(
                        int(uint_a),
                        _decode_text(s1),
                        _decode_text(s2),
                        _decode_text(s3),
                        _decode_text(s4),
                        _decode_text(s5),
                        _decode_text(s6),
                        int(size_a),
                        maybe(SerMessage, msg_h),
                    ),
            )
        )

        callbacks["customLogSink"] = ffi.customLogSink_t(
            make_void(
                "customLogSink",
                lambda cb, level, message:
                    cb(int(level), _decode_text(message) or ""),
            )
        )

        callbacks["hookpointMasterServerHandleOperationRequestJoinGame"] = (
            ffi.hookpointMasterServerHandleOperationRequestJoinGame_t(
                make_bool(
                    "hookpointMasterServerHandleOperationRequestJoinGame",
                    on_missing=True,
                    on_exception=False,
                    adapter=lambda cb, handler_h, text, flag_a:
                        cb(
                            maybe(HandlerBase, handler_h),
                            _decode_text(text),
                            bool(flag_a),
                        ),
                )
            )
        )

        callbacks["hookpointMasterServerHandleOperationRequestCreateGame"] = (
            ffi.hookpointMasterServerHandleOperationRequestCreateGame_t(
                make_bool(
                    "hookpointMasterServerHandleOperationRequestCreateGame",
                    on_missing=True,
                    on_exception=False,
                    adapter=lambda cb, handler_h, text:
                        cb(maybe(HandlerBase, handler_h), _decode_text(text)),
                )
            )
        )

        callbacks["hookpointHandlerBaseHandleENetCommandOnMessage"] = (
            ffi.hookpointHandlerBaseHandleENetCommandOnMessage_t(
                make_bool(
                    "hookpointHandlerBaseHandleENetCommandOnMessage",
                    on_missing=True,
                    on_exception=False,
                    adapter=lambda cb, handler_h, msg_h:
                        cb(maybe(HandlerBase, handler_h), maybe(SerMessage, msg_h)),
                )
            )
        )

        callbacks["hookpointAppLoadAppSettings"] = ffi.hookpointAppLoadAppSettings_t(
            make_bool(
                "hookpointAppLoadAppSettings",
                on_missing=True,
                on_exception=False,
                adapter=lambda cb, app_ptr, bool_ptr:
                    cb(int(app_ptr or 0), BoolRef(bool_ptr)),
            )
        )

        for field_name, callback_value in callbacks.items():
            ctypes.Structure.__setattr__(imports, field_name, callback_value)

        self._ffi_imports = imports
        self._ffi_callbacks = callbacks
        return imports


# ---------------------------------------------------------------------------
# Main API object
# ---------------------------------------------------------------------------

class LuxonServer:
    def __init__(self, lib: ctypes.CDLL) -> None:
        self._lib = lib
        self._server_imports: ServerImports | None = None
        self._ffi_server_imports: ffi.LuxonServerImports | None = None

    def __repr__(self) -> str:
        return f"<LuxonServer features={self.features}>"

    @classmethod
    def from_cdll(cls, lib: ctypes.CDLL) -> "LuxonServer":
        try:
            ffi.bind_luxon_server(lib)
        except AttributeError as exc:
            raise ImportError("Loaded library does not match the expected Luxon server ABI") from exc
        return cls(lib)

    @classmethod
    def load(cls, path: str | os.PathLike[str] | os.PathLike[bytes] | None = None, **cdll_kwargs: Any) -> "LuxonServer":
        if path is not None:
            return cls.from_cdll(ctypes.CDLL(os.fspath(path), **cdll_kwargs))

        errors: list[str] = []
        for candidate in _default_library_candidates():
            try:
                return cls.from_cdll(ctypes.CDLL(candidate, **cdll_kwargs))
            except OSError as exc:
                errors.append(f"{candidate!r}: {exc}")
            except ImportError:
                raise

        tried = ", ".join(_default_library_candidates())
        details = "\n".join(errors) if errors else "(no loader diagnostics)"
        raise OSError(f"Unable to load Luxon server library. Tried: {tried}\n{details}")

    @property
    def lib(self) -> ctypes.CDLL:
        return self._lib

    @property
    def features(self) -> LibraryFeatures:
        return LibraryFeatures(
            set_server_imports=hasattr(self._lib, "luxonSetServerImports"),
            game_plugin=hasattr(self._lib, "registerGamePlugin"),
            auth_plugin=hasattr(self._lib, "registerAuthPlugin"),
            hookpoints=hasattr(self._lib, "registerHookpoints"),
            custom_log_sink=hasattr(self._lib, "luxonEnableCustomLogSink"),
        )

    @property
    def server_imports(self) -> ServerImports | None:
        return self._server_imports

    def _require_export(self, export: str) -> None:
        if not hasattr(self._lib, export):
            raise FeatureUnavailableError(f"Native library does not export {export}")

    def _require_non_null(self, handle: Any, operation: str) -> int:
        value = _normalize_handle(handle)
        if not value:
            raise NullHandleError(f"{operation} returned a null native handle")
        return value

    def _raise_last_error_if_any(self) -> None:
        if bool(self._lib.luxonHasError()):
            error_type = _decode_text(self._lib.luxonGetLastErrorType())
            message = _decode_text(self._lib.luxonGetLastErrorMessage()) or "Unknown Luxon error"
            self._lib.luxonClearLastError()
            raise LuxonError(message, error_type=error_type)

    def _call(self, name: str, *args: Any, check_error: bool = True) -> Any:
        try:
            func = getattr(self._lib, name)
        except AttributeError as exc:
            raise FeatureUnavailableError(f"Native library does not export {name}") from exc
        result = func(*args)
        if check_error:
            self._raise_last_error_if_any()
        return result

    def has_error(self) -> bool:
        return bool(self._lib.luxonHasError())

    def get_last_error(self) -> tuple[str | None, str | None]:
        return (
            _decode_text(self._lib.luxonGetLastErrorType()),
            _decode_text(self._lib.luxonGetLastErrorMessage()),
        )

    @property
    def last_error(self) -> tuple[str | None, str | None]:
        return self.get_last_error()

    def clear_last_error(self) -> None:
        self._lib.luxonClearLastError()

    @contextlib.contextmanager
    def _coerce_ser_value(
        self,
        value: SerValue | ByteArray | BufferLike | None,
        *,
        empty_if_none: bool = False,
    ) -> Iterator[SerValue]:
        if isinstance(value, SerValue):
            yield value
            return

        if value is None:
            if not empty_if_none:
                raise TypeError("Expected SerValue or serialized SerValue bytes, got None")
            temp = self.create_ser_value()
            try:
                yield temp
            finally:
                temp.close()
            return

        temp = self.create_ser_value()
        try:
            temp.deserialize(value)
            yield temp
        finally:
            temp.close()

    @contextlib.contextmanager
    def _coerce_ser_message(
        self,
        value: SerMessage | ByteArray | BufferLike | None,
        *,
        empty_if_none: bool = False,
    ) -> Iterator[SerMessage]:
        if isinstance(value, SerMessage):
            yield value
            return

        if value is None:
            if not empty_if_none:
                raise TypeError("Expected SerMessage or serialized SerMessage bytes, got None")
            temp = self.create_ser_message()
            try:
                yield temp
            finally:
                temp.close()
            return

        temp = self.create_ser_message()
        try:
            temp.deserialize(value)
            yield temp
        finally:
            temp.close()

    # ---- imports / plugins / hookpoints

    def set_server_imports(self, imports: ServerImports | None = None) -> ServerImports:
        self._require_export("luxonSetServerImports")
        imports = imports or ServerImports()
        imports._warn_for_unavailable_features(self.features)
        ffi_imports = imports.build(self)
        self._call("luxonSetServerImports", ctypes.byref(ffi_imports))
        self._server_imports = imports
        self._ffi_server_imports = ffi_imports
        return imports

    def register_game_plugin(self, name: str | bytes) -> int:
        self._require_export("registerGamePlugin")
        return int(self._call("registerGamePlugin", _encode_text(name)))

    def register_auth_plugin(self, flags: int = 0) -> bool:
        self._require_export("registerAuthPlugin")
        return bool(self._call("registerAuthPlugin", int(flags)))

    def register_hookpoints(self, manager: ServerManager | int) -> None:
        self._require_export("registerHookpoints")
        self._call("registerHookpoints", _handle_of(manager))

    def enable_custom_log_sink(self, enabled: bool = True) -> None:
        self._require_export("luxonEnableCustomLogSink")
        self._call("luxonEnableCustomLogSink", bool(enabled))

    def install_python_logging_sink(
        self,
        logger: logging.Logger | None = None,
        *,
        imports: ServerImports | None = None,
    ) -> ServerImports:
        logger = logger or logging.getLogger("luxon.native")
        imports = imports or self._server_imports or ServerImports()

        @imports.custom_log_sink
        def _sink(level: int, message: str) -> None:
            logger.log(_native_log_level_to_python(level), message)

        self.set_server_imports(imports)
        self.enable_custom_log_sink(True)
        return imports

    # ---- factory methods

    def create_byte_array(self, data: ByteArray | BufferLike | None = None) -> ByteArray:
        handle = self._require_non_null(self._call("createByteArray"), "createByteArray")
        out = ByteArray(self, handle, owned=True)
        if data is not None:
            out.copy_from(data if not isinstance(data, ByteArray) else data.to_bytes())
        return out

    def create_ser_value(self, data: SerValue | ByteArray | BufferLike | None = None) -> SerValue:
        handle = self._require_non_null(self._call("createSerValue"), "createSerValue")
        out = SerValue(self, handle, owned=True)
        if data is not None:
            out.deserialize(data)
        return out

    def create_ser_message(self, data: SerMessage | ByteArray | BufferLike | None = None) -> SerMessage:
        handle = self._require_non_null(self._call("createSerMessage"), "createSerMessage")
        out = SerMessage(self, handle, owned=True)
        if data is not None:
            out.deserialize(data)
        return out

    def get_or_create_logger(self, name: str | bytes) -> Logger:
        handle = self._require_non_null(self._call("getOrCreateLogger", _encode_text(name)), "getOrCreateLogger")
        return Logger(self, handle, name=_decode_text(name))

    def create_game_peer_container(self) -> GamePeer:
        handle = self._require_non_null(self._call("createGamePeerContainer"), "createGamePeerContainer")
        return GamePeer(self, handle, owned=True)

    def create_event(self) -> Event:
        handle = self._require_non_null(self._call("createEvent"), "createEvent")
        return Event(self, handle, owned=True)

    def create_lobby(self, name: str | bytes | None, lobby_type: int) -> Lobby:
        handle = self._require_non_null(self._call("createLobby", _encode_text(name), int(lobby_type)), "createLobby")
        return Lobby(self, handle, owned=True)

    def create_server_manager_config(self) -> ServerManagerConfig:
        handle = self._require_non_null(self._call("createServerManagerConfig"), "createServerManagerConfig")
        return ServerManagerConfig(self, handle, owned=True)

    def load_server_manager_config_from_file(
        self,
        path: str | bytes | os.PathLike[str] | os.PathLike[bytes],
    ) -> ServerManagerConfig:
        handle = self._require_non_null(
            self._call("loadServerManagerConfigFromFile", _encode_text(path)),
            "loadServerManagerConfigFromFile",
        )
        return ServerManagerConfig(self, handle, owned=True)

    def parse_server_manager_config(self, contents: str | bytes) -> ServerManagerConfig:
        handle = self._require_non_null(
            self._call("parseServerManagerConfig", _encode_text(contents)),
            "parseServerManagerConfig",
        )
        return ServerManagerConfig(self, handle, owned=True)

    def create_server_manager_from_config(self, config: ServerManagerConfig | int) -> ServerManager:
        handle = self._require_non_null(
            self._call("createServerManagerFromConfig", _handle_of(config.detach())),
            "createServerManagerFromConfig",
        )
        return ServerManager(self, handle, owned=True)

    def create_server_manager_from_file(
        self,
        path: str | bytes | os.PathLike[str] | os.PathLike[bytes],
    ) -> ServerManager:
        handle = self._require_non_null(
            self._call("createServerManagerFromFile", _encode_text(path)),
            "createServerManagerFromFile",
        )
        return ServerManager(self, handle, owned=True)

    def create_server_manager_from_contents(self, contents: str | bytes) -> ServerManager:
        handle = self._require_non_null(
            self._call("createServerManagerFromContents", _encode_text(contents)),
            "createServerManagerFromContents",
        )
        return ServerManager(self, handle, owned=True)

    # ---- misc helpers

    def matches_filter(
        self,
        filter_value: SerValue | ByteArray | BufferLike,
        candidate_value: SerValue | ByteArray | BufferLike,
    ) -> bool:
        with self._coerce_ser_value(filter_value) as filter_sv, self._coerce_ser_value(candidate_value) as candidate_sv:
            return bool(self._call("gameMatchesFilter", filter_sv.handle, candidate_sv.handle))


Luxon = LuxonServer


# ---------------------------------------------------------------------------
# Loader helpers
# ---------------------------------------------------------------------------

def _default_library_candidates() -> list[str]:
    candidates: list[str] = []

    for probe in ("luxon_server", "luxonserver"):
        found = ctypes.util.find_library(probe)
        if found:
            candidates.append(found)

    if sys.platform == "win32":
        candidates += ["luxon_server.dll", "luxonserver.dll"]
    elif sys.platform == "darwin":
        candidates += ["libluxon_server.dylib", "luxon_server.dylib"]
    else:
        candidates += ["libluxon_server.so", "luxon_server.so"]

    seen: set[str] = set()
    unique: list[str] = []
    for item in candidates:
        if item and item not in seen:
            seen.add(item)
            unique.append(item)
    return unique


def from_cdll(lib: ctypes.CDLL) -> LuxonServer:
    return LuxonServer.from_cdll(lib)


def load(path: str | os.PathLike[str] | os.PathLike[bytes] | None = None, **cdll_kwargs: Any) -> LuxonServer:
    return LuxonServer.load(path, **cdll_kwargs)

#pragma once

#include "global.hpp"

namespace server {
namespace IPCEventCodes {
enum Enum : uint8_t { GameUpdate, GameDelete, PersistentPeerStore };
}
} // namespace server

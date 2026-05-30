#pragma once

#ifdef LUXON_SERVER_ENABLE_HOOKPOINTS
#include "global.hpp"
#include "handler_base.hpp"

#include <string>
#include <functional>
#include <luxon/ser_types.hpp>

namespace luxon::enet {
class EnetCommandHeader;
}

namespace server {
class HandlerBase;
class MasterServerHandler;
struct AppSettings;

struct Hookpoints {
    std::function<bool(MasterServerHandler&, const std::string&, bool)> MasterServer_HandleOperationRequest_JoinGame;
    std::function<bool(MasterServerHandler&, const std::string&)> MasterServer_HandleOperationRequest_CreateGame;
    std::function<bool(HandlerBase&, ser::Message&, enet::EnetCommandHeader&)> HandlerBase_HandleENetCommand_OnMessage;
    std::function<bool(App&, AppSettings&, bool& success)> App_load_app_settings;
};
} // namespace server

#define LUXON_SERVER_HOOKPOINT_CSM(custom_server_manager, name, ...)                                                                                           \
    if (custom_server_manager.hookpoints.name &&                                                                                                               \
        (custom_server_manager.hookpoints.name(*this, __VA_ARGS__) || custom_server_manager.should_abort_active_command()))                                    \
    return
#define LUXON_SERVER_HOOKPOINT(name, ...) LUXON_SERVER_HOOKPOINT_CSM(server_manager_, name, __VA_ARGS__)
#else
#define LUXON_SERVER_HOOKPOINT_CSM(...)
#define LUXON_SERVER_HOOKPOINT(...)
#endif

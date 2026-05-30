#include "command_restarter.hpp"

#include <memory>

namespace server {
std::unique_ptr<CommandRestarter> CommandRestarter::create(const std::shared_ptr<HandlerBase>& handler, const enet::EnetCommand& command) {
    return std::unique_ptr<CommandRestarter>(new CommandRestarter(handler, command));
}

void CommandRestarter::restart(std::unique_ptr<CommandRestarter>&& command) {
    auto temp = std::move(command);
    command.reset();
    temp->restart();
}

void CommandRestarter::restart() {
    if (auto& peer = handler_->get_peer())
        if (auto& enet_peer = peer->enet_peer)
            enet_peer->on_payload_command(std::move(command_));
}
} // namespace server

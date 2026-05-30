#pragma once

#include "global.hpp"
#include "handler_base.hpp"

#include <memory>
#include <luxon/enet_protocol.hpp>

namespace server {
class CommandRestarter {
    const std::shared_ptr<HandlerBase> handler_;
    enet::EnetCommand command_;

    CommandRestarter(std::shared_ptr<HandlerBase> handler, enet::EnetCommand command) : handler_(std::move(handler)), command_(std::move(command)) {}

    void restart();

public:
    static std::unique_ptr<CommandRestarter> create(const std::shared_ptr<HandlerBase>& handler, const enet::EnetCommand& command);
    static void restart(std::unique_ptr<CommandRestarter>&& command);
};
} // namespace server

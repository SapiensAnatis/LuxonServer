// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "handler_nameserver.hpp"
#include "global.hpp"
#include "server_manager.hpp"
#include "authentication.hpp"

#include <luxon/ser_interface.hpp>
#include <luxon/common_codes.hpp>
#include <tracy/Tracy.hpp>

namespace server {
void NameServerHandler::HandleOperationRequest(ser::OperationRequestMessage&& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) {
    ZoneScoped;

    if (cmd_header.channel_id != 0)
        return HandlerBase::HandleOperationRequest(std::move(req), is_encrypted, cmd_header);

    if (!peer_->is_authenticated()) {
        switch (req.operation_code) {

        case OpCodes::Auth::Authenticate:
        case OpCodes::Auth::AuthenticateOnce: {
            ZoneScopedN("HandleOperationRequest_Authenticate");

            // Try to authenticate
            auto resp = authenticate(server_manager_, *peer_, req, cmd_header);

            // Add details if authentication was successful
            if (resp.return_code == ErrorCodes::Core::Ok) {
                resp.parameters[DictKeyCodes::LoadBalancing::UserId] = peer_->persistent->user_id;
                resp.parameters[DictKeyCodes::LoadBalancing::Address] =
                    server_manager_.get_endpoint_of(ServerType::MasterServer, peer_->transport_protocol).address;
            }

            // Send payload
            send(proto_->Serialize(resp, is_encrypted));

            // Disconnect on error
            if (!peer_->is_authenticated())
                peer_->disconnect();

            return;
        }

        case OpCodes::RpcAndMisc::GetRegions: {
            ZoneScopedN("HandleOperationRequest_GetRegions");

            // Give dummy response  TODO: Give real response
            ser::OperationResponseMessage resp{.operation_code = OpCodes::RpcAndMisc::GetRegions, .return_code = 0};
            resp.parameters[DictKeyCodes::AuthAndLobby::Region] = std::vector<std::string>{"eu"};
            resp.parameters[DictKeyCodes::LoadBalancing::Address] =
                std::vector<std::string>{server_manager_.get_endpoint_of(ServerType::MasterServer, peer_->transport_protocol).address};
            send(proto_->Serialize(resp));
            return;
        }
        }
    }

    return HandlerBase::HandleOperationRequest(std::move(req), is_encrypted, cmd_header);
}
} // namespace server

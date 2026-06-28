// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "logger.hpp"

#include <memory>
#include <luxon/enet_peer.hpp>
#include <luxon/ser_interface.hpp>
#include <luxon/ser_encryption.hpp>

namespace server {
struct PeerPersistent;
struct Game;

struct Peer {
    std::unique_ptr<ser::IProtocol> protocol;
    std::shared_ptr<enet::EnetPeer> enet_peer;
    std::shared_ptr<logger> log;
    std::unique_ptr<PeerPersistent> persistent;
    ServerProtocol transport_protocol = ServerProtocol::UDP;

    ///
    /// \brief Checks if client has successfully authenticated with the server
    /// \return True if the client is authenticated, otherwise false
    ///
    bool is_authenticated() const { return persistent != nullptr; }
    ///
    /// \brief Enqueues a payload to be sent to the client
    /// \param payload Payload to send
    /// \param opt Options to send payload with
    ///
    void send(const ser::ByteArray& payload, const enet::EnetSendOptions& opt);
    ///
    /// \brief Disconnects the client immediately
    ///
    void disconnect();
};
} // namespace server

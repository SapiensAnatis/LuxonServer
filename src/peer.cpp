// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "peer.hpp"
#include "global.hpp"
#include "game.hpp"
#include "peer_persistence.hpp"

#include <luxon/visualizer.hpp>
#include <tracy/Tracy.hpp>

namespace server {
void Peer::send(const ser::ByteArray& payload, const enet::EnetSendOptions& opt) {
    ZoneScoped;

#ifndef NDEBUG
    log->trace("Sending message using mode {} on channel {}:", static_cast<int>(opt.mode), opt.channel);
    visualizer::print_ser_message(payload, 2, *protocol);
#endif
    enet_peer->send_payload(payload, opt);
}

void Peer::disconnect() { enet_peer->disconnect(); }
} // namespace server

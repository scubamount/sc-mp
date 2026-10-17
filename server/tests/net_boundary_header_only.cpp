// SPDX-License-Identifier: GPL-3.0-only
// A file that includes the two headers the rest of the server builds on, and nothing else. The test
// net_boundary_checks compiles it with the repository root as the only include path, to prove that
// server/net_boundary.h and plugins/multiplayer/relay_wire.h need nothing from sco-core: they must compile where
// sco-core's headers cannot be found. It is never linked or run.
#include "server/net_boundary.h"
#include "plugins/multiplayer/relay_wire.h"

namespace {

// Uses a few types and constants of each header, so that a missing declaration is an error here.
bool UsesTheHeaders() {
    sco::relay::Address address;
    address.family = sco::relay::Address::kIpv4;
    sco::relay::AddressRange range;
    range.bits = 8;
    sco::relay::HostRequest request;
    request.seats = sco::relay::kSessionSeats;
    sco::relay::SessionEvents events;
    sco::relay::wire::RelayRoster roster;
    roster.maxPlayers = static_cast<uint16_t>(sco::relay::kSessionSeats - 1);
    const std::vector<uint8_t> bytes = sco::relay::wire::EncodeRelay(roster);
    return address == address && range.bits == 8 && request.seats == sco::relay::kSessionSeats && !events.message &&
           !bytes.empty() && sco::relay::wire::Fnv1a64("", 0) == 0xcbf29ce484222325ull;
}

}  // namespace

bool BoundaryHeaderCompilesAlone() { return UsesTheHeaders(); }

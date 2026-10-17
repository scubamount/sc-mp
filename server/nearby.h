// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Nearby-only forwarding: the one rule that decides which players get a copy of someone's pose when
// the nearby_only setting is on (the messages are in plugins/multiplayer/relay_wire.h). The server never reads the
// pose itself. It reads the small header a client puts in front of it: which area the pose is in
// (a hash of the outermost zone's name) and where, in metres, in that zone.
//
// A player gets a copy of a pose when the server knows where that player is, and that place is in the
// same area as the sender and within nearby_distance_m of it. Two cases leave the server not knowing:
// the player has not sent a position yet (just joined) or its latest one is older than
// nearby_stale_ms (a loading screen, a stalled game). Rather than cut such a player off at once, the
// server sends it everything for nearby_stale_ms: from the join in the first case, from the moment its
// position went stale in the second. After that it sends nothing until the player sends a position
// again. So a player that never reports a position is not flooded for ever.
//
// This class holds the positions and the counters; it does not send anything itself. The caller gives
// OnUp a function that queues one copy for one slot, so the rule can be tested without a network.
#include "server/net_boundary.h"
#include "server/roster.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

namespace sco::relay {

struct NearbyCounts {
    uint64_t sent = 0;                  // copies queued, one per receiver
    uint64_t sentWithoutPosition = 0;   // of those, copies sent only because the receiver's position was missing or stale
    uint64_t heldBack = 0;              // copies not sent: the receiver is far away or in another area, or its missing/stale position has used up its nearby_stale_ms window
    uint64_t notSent = 0;               // copies the library would not queue (the receiver has not registered its channel yet, or its queue is full)
    uint64_t badUp = 0;                 // near_up messages dropped: not valid, or from someone not in the session
};

class NearbyForwarder {
public:
    // Queues one copy for `slot`; false if it could not be queued.
    using SendCopy = std::function<bool(uint32_t slot, const uint8_t* data, size_t len)>;

    NearbyForwarder(uint32_t distanceM, uint32_t staleMs) : distanceM_(distanceM), staleMs_(staleMs) {}

    void PlayerJoined(PeerId peer, uint64_t nowMs);
    void PlayerLeft(PeerId peer);
    void Clear() { places_.clear(); }

    // One near_up message from `from`, as the library delivered it. Records the sender's position,
    // then calls `send` once for each other player that gets a copy of the pose.
    void OnUp(const Roster& roster, PeerId from, const uint8_t* data, size_t len, uint64_t nowMs, const SendCopy& send);

    const NearbyCounts& Counts() const { return counts_; }

private:
    struct Place {
        bool     known = false;   // has sent a valid position since joining
        uint64_t area = 0;
        double   x = 0, y = 0, z = 0;
        uint64_t updatedMs = 0;   // when the latest position arrived
        uint64_t joinedMs = 0;
    };
    enum class Verdict : uint8_t { Send, SendWithoutPosition, HoldBack };

    Verdict Judge(const Place& receiver, const Place& sender, uint64_t nowMs) const;
    bool IsNear(const Place& receiver, const Place& sender) const;

    uint32_t                  distanceM_;
    uint32_t                  staleMs_;
    std::map<PeerId, Place>   places_;
    NearbyCounts              counts_;
};

}  // namespace sco::relay

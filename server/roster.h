// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Who is in the session, and in which slot. A slot is a small number the server hands out to each
// player, the lowest free one, 0 to max_players - 1. The slot gives the player a channel of its own
// (multiplayer.near.<slot>) when nearby-only is on. Its generation goes up by one each time someone
// new takes the slot, so a message for an earlier occupant can be told apart from one for the present
// one (plugins/multiplayer/relay_wire.h). Peer numbers are never reused; slots are, and that is the reason for the
// generation.
#include "server/net_boundary.h"
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sco::relay {

struct Seat {
    PeerId      peer = 0;         // 0: the slot is free
    uint8_t     generation = 0;   // 0 until the first player takes the slot, then 1, 2, ... and round again after 255
    std::string name;
};

class Roster {
public:
    explicit Roster(uint32_t slots) : seats_(slots) {}

    // Seats the player in the lowest free slot and returns it; -1 when every slot is taken. A player
    // who is already seated keeps the slot.
    int Add(PeerId peer, std::string_view name);
    // Frees the player's slot. false, and *name untouched, when the player is not seated.
    bool Remove(PeerId peer, std::string* name);
    // The slot of a seated player, or -1.
    int SlotOf(PeerId peer) const;

    // One entry per slot, in slot order; a free slot has peer 0.
    const std::vector<Seat>& Seats() const { return seats_; }
    uint32_t Count() const { return static_cast<uint32_t>(slotOf_.size()); }
    // Everybody out and every generation back to 0: the session ended.
    void Clear();

private:
    std::vector<Seat>             seats_;
    std::map<PeerId, uint32_t>    slotOf_;
};

}  // namespace sco::relay

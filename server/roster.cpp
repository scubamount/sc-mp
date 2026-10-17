// SPDX-License-Identifier: GPL-3.0-only
// The roster of players and their slots: the code behind server/roster.h.
// What a slot and its generation are is described in server/roster.h.
#include "server/roster.h"

namespace sco::relay {

int Roster::Add(PeerId peer, std::string_view name) {
    const int seated = SlotOf(peer);
    if (seated >= 0) return seated;
    for (uint32_t slot = 0; slot < seats_.size(); ++slot) {
        Seat& seat = seats_[slot];
        if (seat.peer != 0) continue;
        seat.peer = peer;
        seat.name = std::string(name);
        ++seat.generation;   // a new occupant; 255 wraps to 0, and only equality is ever asked
        slotOf_[peer] = slot;
        return static_cast<int>(slot);
    }
    return -1;
}

bool Roster::Remove(PeerId peer, std::string* name) {
    const auto found = slotOf_.find(peer);
    if (found == slotOf_.end()) return false;
    Seat& seat = seats_[found->second];
    *name = seat.name;
    seat.peer = 0;
    seat.name.clear();   // the generation stays: the next occupant adds one to it
    slotOf_.erase(found);
    return true;
}

int Roster::SlotOf(PeerId peer) const {
    const auto found = slotOf_.find(peer);
    return found == slotOf_.end() ? -1 : static_cast<int>(found->second);
}

void Roster::Clear() {
    for (Seat& seat : seats_) seat = Seat();
    slotOf_.clear();
}

}  // namespace sco::relay

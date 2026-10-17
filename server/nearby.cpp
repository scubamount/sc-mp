// SPDX-License-Identifier: GPL-3.0-only
// Nearby-only forwarding: the code behind server/nearby.h, which decides who gets a copy of a pose.
// The rule and its settings are described in server/nearby.h.
#include "server/nearby.h"
#include "plugins/multiplayer/relay_wire.h"
#include <vector>

namespace sco::relay {

void NearbyForwarder::PlayerJoined(PeerId peer, uint64_t nowMs) {
    Place place;
    place.joinedMs = nowMs;
    places_[peer] = place;
}

void NearbyForwarder::PlayerLeft(PeerId peer) { places_.erase(peer); }

bool NearbyForwarder::IsNear(const Place& receiver, const Place& sender) const {
    if (receiver.area != sender.area) return false;
    const double dx = receiver.x - sender.x;
    const double dy = receiver.y - sender.y;
    const double dz = receiver.z - sender.z;
    const double limit = static_cast<double>(distanceM_);
    return dx * dx + dy * dy + dz * dz <= limit * limit;
}

NearbyForwarder::Verdict NearbyForwarder::Judge(const Place& receiver, const Place& sender, uint64_t nowMs) const {
    const uint64_t since = receiver.known ? receiver.updatedMs : receiver.joinedMs;
    const uint64_t quietMs = nowMs > since ? nowMs - since : 0;
    if (receiver.known && quietMs <= staleMs_) return IsNear(receiver, sender) ? Verdict::Send : Verdict::HoldBack;

    // The server does not know where this receiver is. Send it everything, but only for staleMs_:
    // from the join if it never sent a position, from the moment its position went stale if it did.
    const uint64_t graceStartMs = receiver.known ? staleMs_ : 0;
    const bool inGrace = quietMs >= graceStartMs && quietMs - graceStartMs < staleMs_;
    return inGrace ? Verdict::SendWithoutPosition : Verdict::HoldBack;
}

void NearbyForwarder::OnUp(const Roster& roster, PeerId from, const uint8_t* data, size_t len, uint64_t nowMs,
                           const SendCopy& send) {
    wire::NearUp up;
    if (roster.SlotOf(from) < 0 || !wire::DecodeNearUp(data, len, &up)) {
        ++counts_.badUp;
        return;
    }
    Place& sender = places_[from];
    sender.known = true;
    sender.area = up.area;
    sender.x = up.x;
    sender.y = up.y;
    sender.z = up.z;
    sender.updatedMs = nowMs;

    // One copy of the message for everybody; only the generation byte differs from player to player.
    std::vector<uint8_t> copy = wire::EncodeNearDown(0, from, up.pose, up.poseLen);
    const std::vector<Seat>& seats = roster.Seats();
    for (uint32_t slot = 0; slot < seats.size(); ++slot) {
        const Seat& seat = seats[slot];
        if (seat.peer == 0 || seat.peer == from) continue;
        const Verdict verdict = Judge(places_[seat.peer], sender, nowMs);
        if (verdict == Verdict::HoldBack) {
            ++counts_.heldBack;
            continue;
        }
        copy[wire::kNearDownGenerationOffset] = seat.generation;
        if (!send(slot, copy.data(), copy.size())) {
            ++counts_.notSent;
            continue;
        }
        ++counts_.sent;
        if (verdict == Verdict::SendWithoutPosition) ++counts_.sentWithoutPosition;
    }
}

}  // namespace sco::relay

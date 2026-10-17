// SPDX-License-Identifier: GPL-3.0-only
// Tests of the nearby-only rule (server/nearby.h) without a network: a roster, a forwarder and a
// function that records the copies it is asked to queue. Covers forwarding by area and distance, a
// player whose position is missing or stale (a bounded fallback, counted), an area change, a slot that
// changes hands, malformed position messages and a copy the library will not queue.
#include "server/nearby.h"
#include "plugins/multiplayer/relay_wire.h"
#include "server/roster.h"
#include "server/tests/check.h"
#include <cstdint>
#include <string>
#include <vector>

namespace {

using sco::relay::NearbyCounts;
using sco::relay::NearbyForwarder;
using sco::relay::PeerId;
using sco::relay::Roster;
namespace wire = sco::relay::wire;
using Bytes = std::vector<uint8_t>;

constexpr uint64_t kAreaA = 111;
constexpr uint64_t kAreaB = 222;

struct Copy {
    uint32_t slot;
    Bytes bytes;
};

// Four slots, 12 km, a position stale after 2 s: the defaults of the config file.
struct Rig {
    Roster roster{ 4 };
    NearbyForwarder forwarder{ 12000, 2000 };
    std::vector<Copy> copies;
    bool queueingWorks = true;

    void Join(PeerId peer, uint64_t nowMs) {
        roster.Add(peer, "player");
        forwarder.PlayerJoined(peer, nowMs);
    }
    void Leave(PeerId peer) {
        std::string name;
        roster.Remove(peer, &name);
        forwarder.PlayerLeft(peer);
    }
    void SendUp(PeerId from, const Bytes& message, uint64_t nowMs) {
        forwarder.OnUp(roster, from, message.data(), message.size(), nowMs, [this](uint32_t slot, const uint8_t* data, size_t len) {
            if (!queueingWorks) return false;
            copies.push_back(Copy{ slot, Bytes(data, data + len) });
            return true;
        });
    }
    // A valid position message; the first pose byte tells messages apart.
    void Up(PeerId from, uint64_t area, double x, double y, double z, uint64_t nowMs, uint8_t tag = 0xAA) {
        const Bytes pose = { tag, 1, 2 };
        SendUp(from, wire::EncodeNearUp(area, x, y, z, pose.data(), pose.size()), nowMs);
    }
    // Who got a copy of the latest message, by slot.
    std::vector<uint32_t> Slots() const {
        std::vector<uint32_t> slots;
        for (const Copy& copy : copies) slots.push_back(copy.slot);
        return slots;
    }
    const NearbyCounts& Counts() const { return forwarder.Counts(); }
};

using SlotList = std::vector<uint32_t>;

void TestForwardsToPlayersWithinTheDistance() {
    Rig rig;
    rig.Join(2, 0);   // slot 0
    rig.Join(3, 0);   // slot 1
    rig.Join(4, 0);   // slot 2
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    rig.Up(3, kAreaA, 5000, 0, 0, 100);
    rig.Up(4, kAreaA, 50000, 0, 0, 100);
    rig.copies.clear();

    // Peer 2 reports: peer 3 is 5 km away, peer 4 is 50 km away.
    rig.Up(2, kAreaA, 0, 0, 0, 200, 0x42);
    CHECK(rig.Slots() == SlotList{ 1 });
    if (rig.copies.size() != 1) return;

    // The copy is the pose as sent, from peer 2, for the occupant of slot 1 (generation 1).
    wire::NearDown down;
    CHECK(wire::DecodeNearDown(rig.copies[0].bytes.data(), rig.copies[0].bytes.size(), 1, &down) == wire::NearDownVerdict::Accepted);
    CHECK(down.sender == 2 && down.poseLen == 3 && down.pose[0] == 0x42 && down.pose[1] == 1 && down.pose[2] == 2);
}

void TestTheLimitAndThreeDimensions() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Join(4, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 100);

    // Exactly 12000 m is near; a metre more is not.
    rig.Up(3, kAreaA, 12000, 0, 0, 100);
    rig.Up(4, kAreaA, 12001, 0, 0, 100);
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 200);
    CHECK(rig.Slots() == SlotList{ 1 });

    // Distance is measured in all three directions: 6000 m on each axis is 10392 m, 7000 m on each is 12124 m.
    rig.copies.clear();
    rig.Up(3, kAreaA, 6000, 6000, 6000, 300);
    rig.Up(4, kAreaA, 7000, 7000, 7000, 300);
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 400);
    CHECK(rig.Slots() == SlotList{ 1 });
}

void TestAnotherAreaIsNeverNear() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    rig.Up(3, kAreaB, 0, 0, 0, 100);   // the same coordinates, in a different area
    rig.copies.clear();
    const uint64_t heldBefore = rig.Counts().heldBack;
    rig.Up(2, kAreaA, 0, 0, 0, 200);
    CHECK(rig.copies.empty());
    CHECK(rig.Counts().heldBack == heldBefore + 1);
}

void TestAreaChange() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    rig.Up(3, kAreaA, 100, 0, 0, 100);
    rig.copies.clear();

    rig.Up(2, kAreaA, 0, 0, 0, 200);
    CHECK(rig.Slots() == SlotList{ 1 });                 // same area, near

    rig.copies.clear();
    rig.Up(2, kAreaB, 0, 0, 0, 300);                  // peer 2 walks into another area
    CHECK(rig.copies.empty());                        // peer 3 is still in the first one

    rig.Up(3, kAreaB, 100, 0, 0, 400);                // peer 3 follows; its own report is sent to peer 2
    CHECK(rig.Slots() == SlotList{ 0 });
    rig.copies.clear();
    rig.Up(2, kAreaB, 0, 0, 0, 500);
    CHECK(rig.Slots() == SlotList{ 1 });                 // together again
}

void TestAPlayerWithNoPositionGetsEverythingForAWhile() {
    Rig rig;
    rig.Join(2, 1000);
    rig.Up(2, kAreaA, 0, 0, 0, 1000);
    rig.Join(3, 1000);   // peer 3 never reports a position; it is 100 km from peer 2 in the one it would report
    rig.copies.clear();

    // From its join, for nearby_stale_ms (2000), peer 3 gets every pose of its area or not.
    rig.Up(2, kAreaA, 0, 0, 0, 1500);
    CHECK(rig.Slots() == SlotList{ 1 });
    CHECK(rig.Counts().sentWithoutPosition == 1);
    rig.copies.clear();
    rig.Up(2, kAreaB, 1e9, 0, 0, 2999);               // 1999 ms after the join: still inside the window
    CHECK(rig.Slots() == SlotList{ 1 });
    CHECK(rig.Counts().sentWithoutPosition == 2);

    // Then nothing, until it reports.
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 3000);                 // 2000 ms after the join: over
    CHECK(rig.copies.empty());
    rig.Up(2, kAreaA, 0, 0, 0, 9000);
    CHECK(rig.copies.empty());

    // Its first report ends the fallback: from now on it is judged by where it is.
    rig.Up(3, kAreaA, 100000, 0, 0, 9100);            // 100 km away
    rig.copies.clear();
    const uint64_t withoutBefore = rig.Counts().sentWithoutPosition;
    rig.Up(2, kAreaA, 0, 0, 0, 9200);
    CHECK(rig.copies.empty());
    CHECK(rig.Counts().sentWithoutPosition == withoutBefore);
}

void TestAStalePositionGetsEverythingForAWhileToo() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 0);
    rig.Up(3, kAreaA, 100000, 0, 0, 0);               // peer 3 reports once, far away, then goes quiet
    rig.copies.clear();
    NearbyCounts before = rig.Counts();

    // Up to 2000 ms old the position counts, and says: far away.
    rig.Up(2, kAreaA, 0, 0, 0, 2000);
    CHECK(rig.copies.empty());
    CHECK(rig.Counts().sentWithoutPosition == before.sentWithoutPosition);

    // Older than that it is stale: peer 3 gets everything for another 2000 ms ...
    rig.Up(2, kAreaA, 0, 0, 0, 2001);
    CHECK(rig.Slots() == SlotList{ 1 });
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 3999);
    CHECK(rig.Slots() == SlotList{ 1 });
    CHECK(rig.Counts().sentWithoutPosition == before.sentWithoutPosition + 2);

    // ... and then nothing, until it reports again.
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 4000);
    rig.Up(2, kAreaA, 0, 0, 0, 60000);
    CHECK(rig.copies.empty());
    rig.Up(3, kAreaA, 100, 0, 0, 60100);              // back, and near peer 2
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 60200);
    CHECK(rig.Slots() == SlotList{ 1 });
    CHECK(rig.Counts().sentWithoutPosition == before.sentWithoutPosition + 2);   // this one was by position
}

void TestTheFallbackIsPerReceiver() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 0);
    rig.copies.clear();

    rig.Up(2, kAreaA, 0, 0, 0, 1000);
    CHECK(rig.Slots() == SlotList{ 1 });              // peer 3 is inside its window (joined at 0); peer 4 is not in the session yet

    rig.Join(4, 1500);                                // joins later: its own window runs from 1500 to 3500
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 2500);
    CHECK(rig.Slots() == SlotList{ 2 });              // peer 3's window is over (2500 >= 2000); peer 4's still runs
    rig.copies.clear();
    rig.Up(2, kAreaA, 0, 0, 0, 3600);
    CHECK(rig.copies.empty());                        // and now peer 4's is over too
}

void TestSlotReuse() {
    Rig rig;
    rig.Join(2, 0);   // slot 0, generation 1
    rig.Join(3, 0);   // slot 1, generation 1
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    rig.Up(3, kAreaA, 100, 0, 0, 100);

    rig.Leave(2);
    rig.Join(5, 200);   // takes slot 0: generation 2
    rig.Up(5, kAreaA, 200, 0, 0, 200);
    rig.copies.clear();

    // Peer 3's pose goes to slot 0 and says generation 2 ...
    rig.Up(3, kAreaA, 100, 0, 0, 300);
    CHECK(rig.Slots() == SlotList{ 0 });
    if (rig.copies.size() != 1) return;
    CHECK(rig.copies[0].bytes[wire::kNearDownGenerationOffset] == 2);
    // ... so the new occupant takes it, and a message made for the earlier occupant is dropped by the reader.
    wire::NearDown down;
    const Bytes& bytes = rig.copies[0].bytes;
    CHECK(wire::DecodeNearDown(bytes.data(), bytes.size(), 2, &down) == wire::NearDownVerdict::Accepted);
    CHECK(wire::DecodeNearDown(bytes.data(), bytes.size(), 1, &down) == wire::NearDownVerdict::OtherGeneration);

    // The player who left is gone for good: a message from it counts as bad and reaches no one.
    rig.copies.clear();
    const uint64_t badBefore = rig.Counts().badUp;
    rig.Up(2, kAreaA, 0, 0, 0, 400);
    CHECK(rig.copies.empty());
    CHECK(rig.Counts().badUp == badBefore + 1);
}

void TestMalformedPositionMessages() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    rig.Up(3, kAreaA, 100, 0, 0, 100);
    rig.copies.clear();

    const Bytes pose = { 0xAA, 1, 2 };
    const Bytes good = wire::EncodeNearUp(kAreaB, 5e9, 0, 0, pose.data(), pose.size());   // a valid message from far away, in another area
    Bytes otherVersion = good;
    otherVersion[0] = 2;
    Bytes reservedUsed = good;
    reservedUsed[5] = 1;
    Bytes notANumber = good;
    notANumber[22] = 0xF8;
    notANumber[23] = 0x7F;
    const Bytes headerOnly(good.begin(), good.begin() + 40);
    const Bytes longestPose(wire::kPoseMaxBytes, 0x5A);
    Bytes tooLong = wire::EncodeNearUp(kAreaB, 5e9, 0, 0, longestPose.data(), longestPose.size());
    tooLong.push_back(0x5A);   // a valid header and pose, and one byte too many
    const std::vector<Bytes> bad = { otherVersion, reservedUsed, notANumber, headerOnly, tooLong, Bytes(), Bytes(3, 1) };

    for (const Bytes& message : bad) rig.SendUp(2, message, 200);
    CHECK(rig.copies.empty());
    CHECK(rig.Counts().badUp == bad.size());

    // Bad messages did not move peer 2: it is still where it last reported, near peer 3.
    rig.Up(3, kAreaA, 100, 0, 0, 300);
    CHECK(rig.Slots() == SlotList{ 0 });

    // A valid one is accepted again, and counts nothing as bad.
    CHECK(rig.Counts().badUp == bad.size());
}

void TestNothingGoesBackToTheSender() {
    Rig rig;
    rig.Join(2, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    CHECK(rig.copies.empty());
    CHECK(rig.Counts().sent == 0 && rig.Counts().heldBack == 0 && rig.Counts().notSent == 0);
}

void TestACopyThatCannotBeQueuedIsCounted() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    rig.Up(3, kAreaA, 100, 0, 0, 100);
    const NearbyCounts before = rig.Counts();
    rig.queueingWorks = false;   // the receiver has not registered its channel yet, or its queue is full
    rig.Up(2, kAreaA, 0, 0, 0, 200);
    CHECK(rig.Counts().notSent == before.notSent + 1);
    CHECK(rig.Counts().sent == before.sent);
}

void TestCountsAddUp() {
    Rig rig;
    rig.Join(2, 0);
    rig.Join(3, 0);
    rig.Join(4, 0);
    rig.Up(2, kAreaA, 0, 0, 0, 100);
    rig.Up(3, kAreaA, 100, 0, 0, 100);
    rig.Up(4, kAreaB, 0, 0, 0, 100);
    const NearbyCounts before = rig.Counts();
    rig.Up(2, kAreaA, 0, 0, 0, 200);   // one near (peer 3), one in another area (peer 4)
    CHECK(rig.Counts().sent == before.sent + 1);
    CHECK(rig.Counts().heldBack == before.heldBack + 1);
    CHECK(rig.Counts().sentWithoutPosition == before.sentWithoutPosition);
    CHECK(rig.Counts().notSent == before.notSent && rig.Counts().badUp == before.badUp);
}

}  // namespace

namespace relaytest {

void RunNearbyTests() {
    Run("TestForwardsToPlayersWithinTheDistance", TestForwardsToPlayersWithinTheDistance);
    Run("TestTheLimitAndThreeDimensions", TestTheLimitAndThreeDimensions);
    Run("TestAnotherAreaIsNeverNear", TestAnotherAreaIsNeverNear);
    Run("TestAreaChange", TestAreaChange);
    Run("TestAPlayerWithNoPositionGetsEverythingForAWhile", TestAPlayerWithNoPositionGetsEverythingForAWhile);
    Run("TestAStalePositionGetsEverythingForAWhileToo", TestAStalePositionGetsEverythingForAWhileToo);
    Run("TestTheFallbackIsPerReceiver", TestTheFallbackIsPerReceiver);
    Run("TestSlotReuse", TestSlotReuse);
    Run("TestMalformedPositionMessages", TestMalformedPositionMessages);
    Run("TestNothingGoesBackToTheSender", TestNothingGoesBackToTheSender);
    Run("TestACopyThatCannotBeQueuedIsCounted", TestACopyThatCannotBeQueuedIsCounted);
    Run("TestCountsAddUp", TestCountsAddUp);
}

}  // namespace relaytest

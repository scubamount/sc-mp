// SPDX-License-Identifier: GPL-3.0-only
// Tests of the relay contract bytes (plugins/multiplayer/relay_wire.h): golden byte vectors for the three messages
// and the area hash, so a change of a layout cannot go unnoticed; every way a reader refuses a message;
// the longest legal messages; and the earlier-generation rule. A client plugin or the rig that builds on
// relay_wire.h reads these vectors as the specification.
#include "sco/net/wire.h"
#include "plugins/multiplayer/relay_wire.h"
#include "server/tests/check.h"
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

namespace wire = sco::relay::wire;
using Bytes = std::vector<uint8_t>;

Bytes B(std::initializer_list<int> values) {
    Bytes bytes;
    for (int value : values) bytes.push_back(static_cast<uint8_t>(value));
    return bytes;
}

Bytes Joined(Bytes first, const Bytes& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

const Bytes kPose = B({ 0xAA, 0xBB, 0xCC });

// The area hash is FNV-1a over 64 bits. The first three are the published test vectors of the function.
static_assert(wire::Fnv1a64("", 0) == 0xcbf29ce484222325ull, "FNV-1a of nothing is its offset basis");
static_assert(wire::Fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull, "FNV-1a-64 of a");
static_assert(wire::Fnv1a64("foobar", 6) == 0x85944171f73967e8ull, "FNV-1a-64 of foobar");

void TestAreaHash() {
    CHECK(wire::Fnv1a64("", 0) == 0xcbf29ce484222325ull);
    CHECK(wire::Fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
    CHECK(wire::Fnv1a64("foobar", 6) == 0x85944171f73967e8ull);
    // A zone name as the plugin writes it.
    CHECK(wire::Fnv1a64("OOC_Stanton_2b_Daymar", 21) == 0x7b9a22aa73a012a6ull);
    CHECK(wire::Fnv1a64("Stanton", 7) == 0xa765d3ff038e0deeull);
}

void TestChannelNames() {
    CHECK(std::strcmp(wire::kRelayChannel, "multiplayer.relay") == 0);
    CHECK(std::strcmp(wire::kNearUpChannel, "multiplayer.near_up") == 0);
    CHECK(wire::NearDownChannel(0) == "multiplayer.near.0");
    CHECK(wire::NearDownChannel(49) == "multiplayer.near.49");
    // Every name is one the network library accepts.
    CHECK(sco::net::ValidFqn(wire::kRelayChannel));
    CHECK(sco::net::ValidFqn(wire::kNearUpChannel));
    CHECK(sco::net::ValidFqn(wire::NearDownChannel(0)) && sco::net::ValidFqn(wire::NearDownChannel(999)));
}

void TestSizes() {
    CHECK(wire::kRelayMaxBytes == 1024 && wire::kRelayMaxPlayers == 92);
    CHECK(wire::kNearUpMaxBytes == 1048 && wire::kNearDownMaxBytes == 1024);
    CHECK(wire::kNearUpMaxBytes <= sco::net::kMaxUnreliable && wire::kNearDownMaxBytes <= sco::net::kMaxUnreliable);
}

// ---- multiplayer.relay ---------------------------------------------------------------------------------

wire::RelayRoster TwoPlayers() {
    wire::RelayRoster roster;
    roster.nearbyOnly = true;
    roster.maxPlayers = 15;
    roster.players.push_back(wire::RelayEntry{ 2, 0, 1 });
    roster.players.push_back(wire::RelayEntry{ 5, 3, 2 });
    return roster;
}

void TestRelayGoldenBytes() {
    const Bytes expected = B({ 0x01, 0x01, 0x0F, 0x00, 0x02, 0x00,                                // version 1, nearby-only, 15 players, 2 listed
                               0x02, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x01,                       // peer 2, slot 0, generation 1
                               0x05, 0, 0, 0, 0, 0, 0, 0, 0x03, 0x00, 0x02 });                    // peer 5, slot 3, generation 2
    CHECK(wire::EncodeRelay(TwoPlayers()) == expected);
    CHECK(expected.size() == 28);

    wire::RelayRoster none;
    none.maxPlayers = 15;
    CHECK(wire::EncodeRelay(none) == B({ 0x01, 0x00, 0x0F, 0x00, 0x00, 0x00 }));

    // Little-endian: peer 0x0102030405060708 and a slot over 255.
    wire::RelayRoster big;
    big.maxPlayers = 400;
    big.players.push_back(wire::RelayEntry{ 0x0102030405060708ull, 300, 255 });
    const Bytes bytes = wire::EncodeRelay(big);
    CHECK(bytes == B({ 0x01, 0x00, 0x90, 0x01, 0x01, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x2C, 0x01, 0xFF }));
}

void TestRelayRoundTripAndRefusals() {
    wire::RelayRoster read;
    const Bytes good = wire::EncodeRelay(TwoPlayers());
    CHECK(wire::DecodeRelay(good.data(), good.size(), &read));
    CHECK(read.nearbyOnly && read.maxPlayers == 15 && read.players.size() == 2);
    CHECK(read.players[1].peer == 5 && read.players[1].slot == 3 && read.players[1].generation == 2);

    const auto refused = [&](Bytes bytes) { return !wire::DecodeRelay(bytes.data(), bytes.size(), &read); };
    Bytes otherVersion = good;
    otherVersion[0] = 2;
    CHECK(refused(otherVersion));
    Bytes unknownFlag = good;
    unknownFlag[1] = 0x03;
    CHECK(refused(unknownFlag));
    CHECK(refused(Bytes(good.begin(), good.end() - 1)));          // one byte short
    CHECK(refused(Joined(good, B({ 0 }))));                       // one byte too many
    CHECK(refused(B({ 0x01, 0x00, 0x0F })));                      // no room for the header
    Bytes moreThanMax = good;
    moreThanMax[2] = 1;                                           // maxPlayers 1, two players listed
    CHECK(refused(moreThanMax));
    Bytes slotBeyondMax = good;
    slotBeyondMax[6 + 8] = 15;                                    // slot 15 of 15 (0 to 14)
    CHECK(refused(slotBeyondMax));
    Bytes sameSlot = good;
    sameSlot[6 + 11 + 8] = 0;                                     // both players in slot 0
    CHECK(refused(sameSlot));
    Bytes samePeer = good;
    samePeer[6 + 11] = 2;                                         // both players are peer 2
    CHECK(refused(samePeer));
    Bytes peerZero = good;
    peerZero[6] = 0;
    CHECK(refused(peerZero));
    CHECK(!wire::DecodeRelay(nullptr, 6, &read));
}

void TestRelayLongest() {
    wire::RelayRoster full;
    full.maxPlayers = static_cast<uint16_t>(wire::kRelayMaxPlayers);
    for (size_t slot = 0; slot < wire::kRelayMaxPlayers; ++slot)
        full.players.push_back(wire::RelayEntry{ 2u + slot, static_cast<uint16_t>(slot), 1 });
    const Bytes bytes = wire::EncodeRelay(full);
    CHECK(bytes.size() == 6 + 11 * 92 && bytes.size() <= wire::kRelayMaxBytes);
    wire::RelayRoster read;
    CHECK(wire::DecodeRelay(bytes.data(), bytes.size(), &read) && read.players.size() == 92);

    full.maxPlayers = static_cast<uint16_t>(wire::kRelayMaxPlayers + 1);
    full.players.push_back(wire::RelayEntry{ 99, 92, 1 });
    CHECK(wire::EncodeRelay(full).empty());   // 93 players do not fit in 1024 bytes

    // The decoder holds the limit too. A list of `players` players, made by hand because the encoder
    // would refuse 93, is valid in every field: version, flags, count, distinct peers 2 and up, slots 0 and up.
    const auto list = [](int players) {
        Bytes bytes = B({ 0x01, 0x00, players, 0x00, players, 0x00 });
        for (int i = 0; i < players; ++i) {
            const Bytes entry = B({ 2 + i, 0, 0, 0, 0, 0, 0, 0, i, 0, 1 });
            bytes.insert(bytes.end(), entry.begin(), entry.end());
        }
        return bytes;
    };
    const Bytes ninetyTwo = list(92), ninetyThree = list(93);
    CHECK(ninetyTwo.size() == 1018 && ninetyThree.size() == 1029);
    CHECK(wire::DecodeRelay(ninetyTwo.data(), ninetyTwo.size(), &read));
    CHECK(!wire::DecodeRelay(ninetyThree.data(), ninetyThree.size(), &read));   // only its length is wrong
}

// ---- multiplayer.near_up -------------------------------------------------------------------------------

Bytes GoldenNearUp() {
    return B({ 0x01, 0, 0, 0, 0, 0, 0, 0,                                // version 1, 7 zero bytes
               0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,           // area 0x1122334455667788
               0, 0, 0, 0, 0, 0, 0xF0, 0x3F,                             // x = 1.0
               0, 0, 0, 0, 0, 0, 0x00, 0xC0,                             // y = -2.0
               0, 0, 0, 0, 0, 0, 0xE0, 0x3F,                             // z = 0.5
               0xAA, 0xBB, 0xCC });                                      // the pose, as sent
}

void TestNearUpGoldenBytes() {
    const Bytes bytes = wire::EncodeNearUp(0x1122334455667788ull, 1.0, -2.0, 0.5, kPose.data(), kPose.size());
    CHECK(bytes == GoldenNearUp());
    CHECK(bytes.size() == 43);

    wire::NearUp up;
    CHECK(wire::DecodeNearUp(bytes.data(), bytes.size(), &up));
    CHECK(up.area == 0x1122334455667788ull && up.x == 1.0 && up.y == -2.0 && up.z == 0.5);
    CHECK(up.poseLen == 3 && up.pose == bytes.data() + 40 && std::memcmp(up.pose, kPose.data(), 3) == 0);
}

void TestNearUpRefusals() {
    wire::NearUp up;
    const auto refused = [&](const Bytes& bytes) { return !wire::DecodeNearUp(bytes.data(), bytes.size(), &up); };
    const Bytes golden = GoldenNearUp();

    Bytes otherVersion = golden;
    otherVersion[0] = 2;
    CHECK(refused(otherVersion));
    for (size_t reserved = 1; reserved <= 7; ++reserved) {
        Bytes dirty = golden;
        dirty[reserved] = 1;
        CHECK(refused(dirty));
    }
    Bytes notANumber = golden;
    std::memcpy(notANumber.data() + 16, "\x00\x00\x00\x00\x00\x00\xF8\x7F", 8);   // NaN
    CHECK(refused(notANumber));
    Bytes infinite = golden;
    // +infinity. Written byte by byte with at(): GCC 13 at -O3 takes a memcpy into this copy for a write past the end
    // of an empty vector (-Warray-bounds, a false positive here), and the build treats warnings as errors.
    const uint8_t positiveInfinity[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x7F };
    for (size_t i = 0; i < sizeof positiveInfinity; ++i) infinite.at(24 + i) = positiveInfinity[i];
    CHECK(refused(infinite));
    Bytes beyondTheUniverse = golden;
    std::memcpy(beyondTheUniverse.data() + 32, "\x00\x00\x90\x1E\xC4\xBC\xD6\x42", 8);   // 1e14
    CHECK(refused(beyondTheUniverse));
    CHECK(refused(Bytes(golden.begin(), golden.begin() + 40)));          // a header and no pose
    CHECK(refused(Bytes(golden.begin(), golden.begin() + 20)));          // cut in the middle of the header
    // One byte longer than the longest legal message, with everything else in order: the length alone is refused.
    const Bytes longestPose(wire::kPoseMaxBytes, 0x5A);
    Bytes oneByteTooMany = wire::EncodeNearUp(1, 0, 0, 0, longestPose.data(), longestPose.size());
    CHECK(oneByteTooMany.size() == wire::kNearUpMaxBytes && !refused(oneByteTooMany));
    oneByteTooMany.push_back(0x5A);
    CHECK(oneByteTooMany.size() == wire::kNearUpMaxBytes + 1 && refused(oneByteTooMany));
    CHECK(!wire::DecodeNearUp(nullptr, 43, &up));
}

void TestNearUpLongestAndEncodeRefusals() {
    const Bytes longestPose(wire::kPoseMaxBytes, 0x5A);
    const Bytes longest = wire::EncodeNearUp(1, 0, 0, 0, longestPose.data(), longestPose.size());
    CHECK(longest.size() == wire::kNearUpMaxBytes);
    wire::NearUp up;
    CHECK(wire::DecodeNearUp(longest.data(), longest.size(), &up) && up.poseLen == 1008);

    const Bytes tooLong(wire::kPoseMaxBytes + 1, 0x5A);
    CHECK(wire::EncodeNearUp(1, 0, 0, 0, tooLong.data(), tooLong.size()).empty());
    CHECK(wire::EncodeNearUp(1, 0, 0, 0, kPose.data(), 0).empty());
    CHECK(wire::EncodeNearUp(1, 0, 0, 0, nullptr, 3).empty());
    CHECK(wire::EncodeNearUp(1, 1e300, 0, 0, kPose.data(), kPose.size()).empty());
}

// ---- multiplayer.near.<slot> ---------------------------------------------------------------------------

Bytes GoldenNearDown() {
    return B({ 0x01, 0x02, 0, 0, 0, 0, 0, 0,                             // version 1, generation 2, 6 zero bytes
               0x0A, 0, 0, 0, 0, 0, 0, 0,                                // sender: peer 10
               0xAA, 0xBB, 0xCC });                                      // the pose, as sent
}

void TestNearDownGoldenBytes() {
    const Bytes bytes = wire::EncodeNearDown(2, 10, kPose.data(), kPose.size());
    CHECK(bytes == GoldenNearDown());
    CHECK(bytes.size() == 19 && bytes[wire::kNearDownGenerationOffset] == 2);

    wire::NearDown down;
    CHECK(wire::DecodeNearDown(bytes.data(), bytes.size(), 2, &down) == wire::NearDownVerdict::Accepted);
    CHECK(down.sender == 10 && down.poseLen == 3 && down.pose == bytes.data() + 16);
}

void TestEarlierGenerationIsDropped() {
    // The slot had an earlier occupant (generation 1); its successor holds generation 2. A message
    // made for generation 1 is well formed but is not the successor's: it is dropped.
    const Bytes forTheEarlierOne = wire::EncodeNearDown(1, 10, kPose.data(), kPose.size());
    const Bytes forThePresentOne = wire::EncodeNearDown(2, 10, kPose.data(), kPose.size());
    wire::NearDown down;
    CHECK(wire::DecodeNearDown(forTheEarlierOne.data(), forTheEarlierOne.size(), 2, &down) == wire::NearDownVerdict::OtherGeneration);
    CHECK(wire::DecodeNearDown(forThePresentOne.data(), forThePresentOne.size(), 2, &down) == wire::NearDownVerdict::Accepted);
    // And the other way round, for a client that has not yet read the newer list: it drops the newer one.
    CHECK(wire::DecodeNearDown(forThePresentOne.data(), forThePresentOne.size(), 1, &down) == wire::NearDownVerdict::OtherGeneration);
    // The counter wraps after 255; only equality is ever asked.
    const Bytes wrapped = wire::EncodeNearDown(0, 10, kPose.data(), kPose.size());
    CHECK(wire::DecodeNearDown(wrapped.data(), wrapped.size(), 0, &down) == wire::NearDownVerdict::Accepted);
    CHECK(wire::DecodeNearDown(wrapped.data(), wrapped.size(), 255, &down) == wire::NearDownVerdict::OtherGeneration);
}

void TestNearDownRefusals() {
    wire::NearDown down;
    const auto verdict = [&](const Bytes& bytes) { return wire::DecodeNearDown(bytes.data(), bytes.size(), 2, &down); };
    const Bytes golden = GoldenNearDown();
    CHECK(verdict(golden) == wire::NearDownVerdict::Accepted);

    Bytes otherVersion = golden;
    otherVersion[0] = 2;
    CHECK(verdict(otherVersion) == wire::NearDownVerdict::Malformed);
    for (size_t reserved = 2; reserved <= 7; ++reserved) {
        Bytes dirty = golden;
        dirty[reserved] = 1;
        CHECK(verdict(dirty) == wire::NearDownVerdict::Malformed);
    }
    CHECK(verdict(Bytes(golden.begin(), golden.begin() + 16)) == wire::NearDownVerdict::Malformed);   // no pose
    // One byte longer than the longest legal message, with everything else in order: the length alone is refused.
    const Bytes longestPose(wire::kPoseMaxBytes, 0x5A);
    Bytes oneByteTooMany = wire::EncodeNearDown(2, 10, longestPose.data(), longestPose.size());
    CHECK(oneByteTooMany.size() == wire::kNearDownMaxBytes && verdict(oneByteTooMany) == wire::NearDownVerdict::Accepted);
    oneByteTooMany.push_back(0x5A);
    CHECK(oneByteTooMany.size() == wire::kNearDownMaxBytes + 1 && verdict(oneByteTooMany) == wire::NearDownVerdict::Malformed);
    CHECK(wire::DecodeNearDown(nullptr, 19, 2, &down) == wire::NearDownVerdict::Malformed);

    // A message that is malformed AND for another generation is malformed: the shape is judged first.
    Bytes both = golden;
    both[0] = 2;
    both[1] = 9;
    CHECK(verdict(both) == wire::NearDownVerdict::Malformed);
}

void TestNearDownLongest() {
    const Bytes longestPose(wire::kPoseMaxBytes, 0x5A);
    const Bytes longest = wire::EncodeNearDown(7, 99, longestPose.data(), longestPose.size());
    CHECK(longest.size() == wire::kNearDownMaxBytes);
    CHECK(wire::EncodeNearDown(7, 99, longestPose.data(), 0).empty());
    const Bytes tooLong(wire::kPoseMaxBytes + 1, 0x5A);
    CHECK(wire::EncodeNearDown(7, 99, tooLong.data(), tooLong.size()).empty());
}

}  // namespace

namespace relaytest {

void RunRelayWireTests() {
    Run("TestAreaHash", TestAreaHash);
    Run("TestChannelNames", TestChannelNames);
    Run("TestSizes", TestSizes);
    Run("TestRelayGoldenBytes", TestRelayGoldenBytes);
    Run("TestRelayRoundTripAndRefusals", TestRelayRoundTripAndRefusals);
    Run("TestRelayLongest", TestRelayLongest);
    Run("TestNearUpGoldenBytes", TestNearUpGoldenBytes);
    Run("TestNearUpRefusals", TestNearUpRefusals);
    Run("TestNearUpLongestAndEncodeRefusals", TestNearUpLongestAndEncodeRefusals);
    Run("TestNearDownGoldenBytes", TestNearDownGoldenBytes);
    Run("TestEarlierGenerationIsDropped", TestEarlierGenerationIsDropped);
    Run("TestNearDownRefusals", TestNearDownRefusals);
    Run("TestNearDownLongest", TestNearDownLongest);
}

}  // namespace relaytest

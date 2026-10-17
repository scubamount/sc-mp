// SPDX-License-Identifier: GPL-3.0-only
// Tests of the relay messages through the whole server, on sco-core's in-memory network with a simulated
// clock: multiplayer.relay beside relay.info on every join and every interval; slots and generations
// as players come and go; nearby-only forwarding from a player's position message to the copies other
// players receive; a slot that changes hands; a bad position message in the stats; and the start-up
// lines about nearby-only. The players are plain sco-core clients that speak the relay contract.
#include "server/relay_info.h"
#include "plugins/multiplayer/relay_wire.h"
#include "server/run_loop.h"
#include "server/session.h"
#include "server/tests/check.h"
#include "server/tests/session_kit.h"
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace relaytest;
using sco::relay::LogLevel;
using sco::relay::RelaySession;
using sco::relay::Settings;
namespace wire = sco::relay::wire;
using Bytes = std::vector<uint8_t>;

constexpr uint64_t kArea = 0x1122334455667788ull;

Settings NearbySettings(uint32_t maxPlayers) {
    Settings s = TestSettings(maxPlayers);
    s.nearbyOnly = true;
    return s;
}

// A pose the receiver got on its own channel, as the contract reads it.
struct Seen {
    uint64_t sender;
    uint8_t  tag;   // the first byte of the pose, which tells the poses of a test apart
};

// A player that speaks the relay contract: it asks for multiplayer.relay and says where it is on
// multiplayer.near_up when it joins, learns its slot from the list the server sends, and then listens
// on the channel of its slot.
struct Walker {
    Walker(World& w, uint16_t id, const char* playerName) : joiner(w.net, id), name(playerName) {
        joiner.core.RegisterChannel(wire::kRelayChannel, sco::net::kReliable | sco::net::kFromHost, wire::kRelayMaxBytes);
        joiner.core.RegisterChannel(wire::kNearUpChannel, sco::net::kToHost, wire::kNearUpMaxBytes);
    }

    // The newest multiplayer.relay that decodes.
    bool LastRoster(wire::RelayRoster* out) const {
        for (size_t i = joiner.got.size(); i-- > 0;) {
            const Received& r = joiner.got[i];
            if (r.channel == wire::kRelayChannel && wire::DecodeRelay(r.data.data(), r.data.size(), out)) return true;
        }
        return false;
    }

    // Reads the player's own slot and generation from the newest list; the first time, listens on the slot's channel.
    bool LearnSlot() {
        wire::RelayRoster roster;
        if (!LastRoster(&roster)) return false;
        for (const wire::RelayEntry& entry : roster.players) {
            if (entry.peer != joiner.core.Self()) continue;
            if (slot < 0) {
                joiner.core.RegisterChannel(wire::NearDownChannel(entry.slot), sco::net::kFromHost, wire::kNearDownMaxBytes);
            }
            slot = entry.slot;
            generation = entry.generation;
            return true;
        }
        return false;
    }

    bool SendUp(uint64_t area, double x, double y, double z, uint8_t tag = 0xAA) {
        const Bytes pose = { tag, 1, 2 };
        const Bytes message = wire::EncodeNearUp(area, x, y, z, pose.data(), pose.size());
        return joiner.core.Send(wire::kNearUpChannel, message.data(), static_cast<uint32_t>(message.size())) ==
               sco::net::SendResult::Ok;
    }

    // The poses this player accepted on its slot's channel: made for its generation, from whom, tagged how.
    std::vector<Seen> Poses() const {
        std::vector<Seen> seen;
        if (slot < 0) return seen;
        const std::string channel = wire::NearDownChannel(static_cast<uint32_t>(slot));
        for (const Received& r : joiner.got) {
            if (r.channel != channel) continue;
            wire::NearDown down;
            if (wire::DecodeNearDown(r.data.data(), r.data.size(), generation, &down) == wire::NearDownVerdict::Accepted)
                seen.push_back(Seen{ down.sender, down.pose[0] });
        }
        return seen;
    }

    Joiner      joiner;
    const char* name;
    int         slot = -1;
    uint8_t     generation = 0;
};

Joiners JoinersOf(const std::vector<Walker*>& walkers) {
    Joiners all;
    for (Walker* walker : walkers) all.push_back(&walker->joiner);
    return all;
}

const auto kNever = [] { return false; };

// Joins every walker, one after the other, then runs until each knows its slot and listens on it, and
// a little longer so the server knows it too.
bool JoinAll(World& w, RelaySession& session, const std::vector<Walker*>& walkers) {
    const Joiners all = JoinersOf(walkers);
    for (Walker* walker : walkers)
        if (!JoinNow(w, session, all, walker->joiner, walker->name)) return false;
    const bool known = RunUntil(w, session, all, 5000, [&] {
        bool every = true;
        for (Walker* walker : walkers) every = walker->LearnSlot() && every;
        return every;
    });
    RunUntil(w, session, all, 200, kNever);
    return known;
}

void Settle(World& w, RelaySession& session, const std::vector<Walker*>& walkers, uint64_t ms = 300) {
    RunUntil(w, session, JoinersOf(walkers), ms, kNever);
}

void ForgetWhatWasReceived(const std::vector<Walker*>& walkers) {
    for (Walker* walker : walkers) walker->joiner.got.clear();
}

// ---- multiplayer.relay -----------------------------------------------------------------------------

void TestRelayMessageOnJoinAndEveryInterval() {
    World w;
    Settings s = TestSettings(3);
    s.announceIntervalMs = 1000;
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));

    Walker ann(w, 2, "Ann"), bob(w, 3, "Bob");
    ann.joiner.RegisterInfo();   // relay.info is still sent beside the new message, unchanged
    const Joiners all = JoinersOf({ &ann, &bob });
    CHECK(JoinNow(w, session, all, ann.joiner, "Ann"));
    CHECK(RunUntil(w, session, all, 2000, [&] { return ann.joiner.Count("multiplayer.relay") >= 1 && ann.joiner.Count("relay.info") >= 1; }));

    wire::RelayRoster roster;
    CHECK(ann.LastRoster(&roster));
    CHECK(!roster.nearbyOnly && roster.maxPlayers == 3 && roster.players.size() == 1);
    if (roster.players.size() == 1)
        CHECK(roster.players[0].peer == 2 && roster.players[0].slot == 0 && roster.players[0].generation == 1);

    // relay.info is the version 1 message it always was.
    for (const Received& r : ann.joiner.got) {
        if (r.channel != "relay.info") continue;
        sco::relay::RelayInfo info;
        CHECK(sco::relay::DecodeRelayInfo(r.data.data(), r.data.size(), &info) && info.maxPlayers == 3 && r.data.size() == 3);
        break;
    }

    // Bob joins: both players are told the list, with two entries.
    CHECK(JoinNow(w, session, all, bob.joiner, "Bob"));
    CHECK(RunUntil(w, session, all, 2000, [&] {
        wire::RelayRoster a, b;
        return ann.LastRoster(&a) && bob.LastRoster(&b) && a.players.size() == 2 && b.players.size() == 2;
    }));
    CHECK(ann.LastRoster(&roster) && roster.players.size() == 2);
    if (roster.players.size() == 2)
        CHECK(roster.players[1].peer == 3 && roster.players[1].slot == 1 && roster.players[1].generation == 1);

    // And the list is sent again every interval, for as long as someone is in.
    const size_t before = ann.joiner.Count("multiplayer.relay");
    CHECK(RunUntil(w, session, all, 2 * s.announceIntervalMs + 100, [&] { return ann.joiner.Count("multiplayer.relay") >= before + 2; }));
}

void TestTheListSaysWhetherNearbyOnlyIsOn() {
    for (const bool nearby : { false, true }) {
        World w;
        Settings s = TestSettings(3);
        s.nearbyOnly = nearby;
        RelaySession session(w.serverTransport, s, w.log, kTestIters);
        std::string why;
        CHECK(session.Start(0, &why));
        Walker ann(w, 2, "Ann");
        CHECK(JoinAll(w, session, { &ann }));
        wire::RelayRoster roster;
        CHECK(ann.LastRoster(&roster) && roster.nearbyOnly == nearby);
    }
}

void TestSlotsAndGenerationsAsPlayersCome() {
    World w;
    RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    Walker ann(w, 2, "Ann"), bob(w, 3, "Bob"), dan(w, 5, "Dan");
    CHECK(JoinAll(w, session, { &ann, &bob }));
    CHECK(ann.slot == 0 && bob.slot == 1 && ann.generation == 1 && bob.generation == 1);

    // Ann leaves. The next player takes her slot, with the next generation; Bob's slot is untouched.
    ann.joiner.core.Leave("bye");
    const Joiners all = JoinersOf({ &ann, &bob, &dan });
    CHECK(RunUntil(w, session, all, 5000, [&] { return session.PlayerCount() == 1; }));
    CHECK(JoinNow(w, session, all, dan.joiner, "Dan"));
    CHECK(RunUntil(w, session, all, 5000, [&] { return dan.LearnSlot(); }));
    CHECK(dan.slot == 0 && dan.generation == 2);
    CHECK(session.GetRoster().Seats()[0].peer == dan.joiner.core.Self() && session.GetRoster().Seats()[0].generation == 2);
    CHECK(session.GetRoster().Seats()[1].peer == bob.joiner.core.Self() && session.GetRoster().Seats()[1].generation == 1);

    // Bob is told too, on the next list.
    CHECK(RunUntil(w, session, all, 5000, [&] {
        wire::RelayRoster roster;
        return bob.LastRoster(&roster) && roster.players.size() == 2 && roster.players[0].peer == dan.joiner.core.Self() &&
               roster.players[0].generation == 2;
    }));
}

// ---- nearby-only -----------------------------------------------------------------------------------

void TestPosesGoOnlyToNearbyPlayers() {
    World w;
    RelaySession session(w.serverTransport, NearbySettings(3), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    Walker ann(w, 2, "Ann"), bob(w, 3, "Bob"), cid(w, 4, "Cid");
    const std::vector<Walker*> everyone = { &ann, &bob, &cid };
    CHECK(JoinAll(w, session, everyone));
    CHECK(ann.slot == 0 && bob.slot == 1 && cid.slot == 2);

    // Ann and Bob are 5 km apart in one area; Cid is 50 km from them, in the same area.
    CHECK(ann.SendUp(kArea, 0, 0, 0));
    CHECK(bob.SendUp(kArea, 5000, 0, 0));
    CHECK(cid.SendUp(kArea, 50000, 0, 0));
    Settle(w, session, everyone);
    ForgetWhatWasReceived(everyone);

    CHECK(ann.SendUp(kArea, 0, 0, 0, 0x42));
    Settle(w, session, everyone);
    const std::vector<Seen> bobSaw = bob.Poses();
    CHECK(bobSaw.size() == 1);
    if (bobSaw.size() == 1) CHECK(bobSaw[0].sender == 2 && bobSaw[0].tag == 0x42);
    CHECK(cid.Poses().empty());   // too far
    CHECK(ann.Poses().empty());   // never her own

    // Nothing was relayed the plain way: the library passed no pose on a channel of its own.
    CHECK(session.NearbyStats().sent >= 1 && session.NearbyStats().heldBack >= 1);

    // Cid walks to within 12 km of Ann: from his next report on, he gets her poses.
    CHECK(cid.SendUp(kArea, 9000, 0, 0));
    Settle(w, session, everyone);
    ForgetWhatWasReceived(everyone);
    CHECK(ann.SendUp(kArea, 0, 0, 0, 0x43));
    Settle(w, session, everyone);
    CHECK(cid.Poses().size() == 1 && bob.Poses().size() == 1);
}

void TestASlotThatChangesHandsGetsTheNewGeneration() {
    World w;
    RelaySession session(w.serverTransport, NearbySettings(2), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    Walker ann(w, 2, "Ann"), bob(w, 3, "Bob"), dan(w, 5, "Dan");
    CHECK(JoinAll(w, session, { &ann, &bob }));

    ann.joiner.core.Leave("bye");
    const Joiners all = JoinersOf({ &ann, &bob, &dan });
    CHECK(RunUntil(w, session, all, 5000, [&] { return session.PlayerCount() == 1; }));
    CHECK(JoinNow(w, session, all, dan.joiner, "Dan"));
    CHECK(RunUntil(w, session, all, 5000, [&] { return dan.LearnSlot(); }));
    Settle(w, session, { &ann, &bob, &dan });
    CHECK(dan.slot == 0 && dan.generation == 2);   // Ann's slot, the second occupant

    CHECK(dan.SendUp(kArea, 0, 0, 0));
    CHECK(bob.SendUp(kArea, 100, 0, 0));
    Settle(w, session, { &ann, &bob, &dan });
    ForgetWhatWasReceived({ &ann, &bob, &dan });
    CHECK(bob.SendUp(kArea, 100, 0, 0, 0x55));
    Settle(w, session, { &ann, &bob, &dan });

    // Dan gets Bob's pose, made for generation 2; the reader drops the same message as one for Ann's generation 1.
    CHECK(dan.Poses().size() == 1);
    for (const Received& r : dan.joiner.got) {
        if (r.channel != wire::NearDownChannel(0)) continue;
        wire::NearDown down;
        CHECK(r.data[wire::kNearDownGenerationOffset] == 2);
        CHECK(wire::DecodeNearDown(r.data.data(), r.data.size(), 1, &down) == wire::NearDownVerdict::OtherGeneration);
    }
}

void TestABadPositionMessageIsCountedAndWarnedAbout() {
    World w;
    const Settings s = NearbySettings(3);   // a stats line every 1000 ms
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    sco::relay::RunLoop loop(session, s, w.log);

    Walker bob(w, 3, "Bob");
    const Joiners all = JoinersOf({ &bob });
    CHECK(JoinNow(w, session, all, bob.joiner, "Bob"));
    const Bytes garbage(60, 9);   // version 9
    CHECK(bob.joiner.core.Send(wire::kNearUpChannel, garbage.data(), static_cast<uint32_t>(garbage.size())) == sco::net::SendResult::Ok);

    const uint64_t until = w.net.now + 1200;
    for (uint64_t t = w.net.now; t <= until; t += 5) {
        w.net.now = t;
        loop.Pass(t);
        bob.joiner.core.Pump(t);
    }
    CHECK(session.NearbyStats().badUp == 1);
    CHECK(w.CountAt(LogLevel::Warn, "dropped 1 nearby position messages") == 1);
    CHECK(w.CountAt(LogLevel::Warn, "every player runs the same multiplayer plugin build") == 1);
    CHECK(w.Logged("bad position messages 1"));   // the stats line carries the nearby part when nearby-only is on
}

void TestStartLogsTheNearbyMode() {
    {
        World w;
        RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
        std::string why;
        CHECK(session.Start(0, &why));
        CHECK(!w.Logged("nearby-only"));   // off: nothing to say
    }
    {
        World w;
        RelaySession session(w.serverTransport, NearbySettings(3), w.log, kTestIters);
        std::string why;
        CHECK(session.Start(0, &why));
        CHECK(w.CountAt(LogLevel::Info, "nearby-only is on: a player's pose goes only to players in the same area within 12000 m") == 1);
        CHECK(w.CountAt(LogLevel::Warn, "nearby_distance_m") == 0);
    }
    {
        World w;
        Settings s = NearbySettings(3);
        s.nearbyDistanceM = 5000;   // below the 10 km at which the plugin shows players
        RelaySession session(w.serverTransport, s, w.log, kTestIters);
        std::string why;
        CHECK(session.Start(0, &why));
        CHECK(w.CountAt(LogLevel::Warn, "nearby_distance_m is 5000, below the 10000 m") == 1);
    }
}

}  // namespace

namespace relaytest {

void RunNearbySessionTests() {
    Run("TestRelayMessageOnJoinAndEveryInterval", TestRelayMessageOnJoinAndEveryInterval);
    Run("TestTheListSaysWhetherNearbyOnlyIsOn", TestTheListSaysWhetherNearbyOnlyIsOn);
    Run("TestSlotsAndGenerationsAsPlayersCome", TestSlotsAndGenerationsAsPlayersCome);
    Run("TestPosesGoOnlyToNearbyPlayers", TestPosesGoOnlyToNearbyPlayers);
    Run("TestASlotThatChangesHandsGetsTheNewGeneration", TestASlotThatChangesHandsGetsTheNewGeneration);
    Run("TestABadPositionMessageIsCountedAndWarnedAbout", TestABadPositionMessageIsCountedAndWarnedAbout);
    Run("TestStartLogsTheNearbyMode", TestStartLogsTheNearbyMode);
}

}  // namespace relaytest

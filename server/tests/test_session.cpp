// SPDX-License-Identifier: GPL-3.0-only
// Tests of RelaySession on sco-core's in-memory network: joining, relay.info, a wrong passphrase,
// one player over the limit, the library's own limit, relaying without taking part, leaving,
// silence, a restart, the address rule, traffic counting, and two config files giving two
// behaviours. Time is simulated; nothing sleeps.
#include "sco/net/scope.h"
#include "server/address_rule.h"
#include "server/config.h"
#include "server/relay_info.h"
#include "server/session.h"
#include "server/tests/check.h"
#include "server/tests/session_kit.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace relaytest;
using sco::relay::RelaySession;
using sco::relay::Settings;

void TestJoinAndRelayInfo() {
    World w;
    RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    CHECK(session.Running());

    Joiner ann(w.net, 2);
    ann.RegisterInfo();
    CHECK(ann.Join(0, "Ann"));
    CHECK(RunUntil(w, session, { &ann }, 5000, [&] { return ann.Up() && ann.Count("relay.info") == 1; }));
    CHECK(session.PlayerCount() == 1);
    CHECK(w.Logged("session is up") && w.Logged("player 'Ann' joined as peer 2"));

    // Peer 1 is the server, with the configured name.
    std::string serverName;
    CHECK(ann.core.PeerName(sco::net::kHostPeer, &serverName) && serverName == "Relay");

    // relay.info comes from peer 1 and says how many players the server takes.
    CHECK(ann.got.size() == 1);
    if (ann.got.size() != 1) return;
    CHECK(ann.got[0].from == sco::net::kHostPeer);
    sco::relay::RelayInfo info;
    CHECK(sco::relay::DecodeRelayInfo(ann.got[0].data.data(), ann.got[0].data.size(), &info));
    CHECK(info.maxPlayers == 3);
}

void TestClientWithoutRelayInfo() {
    // A client that never registers relay.info is not affected by it.
    World w;
    RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    Joiner ann(w.net, 2);
    CHECK(ann.Join(0, "Ann"));
    CHECK(RunUntil(w, session, { &ann }, 5000, [&] { return ann.Up(); }));
    RunUntil(w, session, { &ann }, 1000, [] { return false; });
    CHECK(ann.Up());
    CHECK(ann.got.empty());
    CHECK(ann.core.GetStats().malformed == 0);
    CHECK(ann.core.GetStats().unknownChannel == 0);
}

void TestWrongPassphrase() {
    World w;
    RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    Joiner mallory(w.net, 2);
    CHECK(mallory.Join(0, "Mallory", "not the passphrase"));
    CHECK(RunUntil(w, session, { &mallory }, 5000, [&] { return mallory.downs == 1; }));
    CHECK(mallory.lastDown == "refused: wrong passphrase");
    CHECK(session.PlayerCount() == 0);
    CHECK(session.CoreStats().joinsRefused == 1);
}

void TestOnePlayerOverTheLimit() {
    World w;
    RelaySession session(w.serverTransport, TestSettings(2), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    Joiner ann(w.net, 2), bob(w.net, 3), cid(w.net, 4);
    const Joiners all = { &ann, &bob, &cid };
    CHECK(JoinNow(w, session, all, ann, "Ann"));
    CHECK(JoinNow(w, session, all, bob, "Bob"));
    CHECK(JoinNow(w, session, all, cid, "Cid") == false);
    CHECK(cid.lastDown == "refused: the session is full");
    CHECK(session.PlayerCount() == 2);
    CHECK(ann.Up() && bob.Up());
}

void TestLibraryLimit() {
    // The most the library allows: every place but the server's own is taken, then one more is refused.
    const uint32_t players = sco::relay::kSessionSeats - 1;
    World w;
    RelaySession session(w.serverTransport, TestSettings(players), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));

    std::vector<std::unique_ptr<Joiner>> crowd;
    Joiners all;
    for (uint32_t i = 0; i < players; ++i) {
        crowd.push_back(std::make_unique<Joiner>(w.net, static_cast<uint16_t>(2 + i)));
        all.push_back(crowd.back().get());
    }
    Joiner extra(w.net, 100);
    all.push_back(&extra);

    for (uint32_t i = 0; i < players; ++i) {
        const std::string name = sco::relay::Format("Player%u", i);
        CHECK(crowd[i]->Join(w.net.now, name.c_str()));
    }
    CHECK(RunUntil(w, session, all, 10000, [&] { return session.PlayerCount() == players; }));
    CHECK(extra.Join(w.net.now, "OneTooMany"));
    CHECK(RunUntil(w, session, all, 5000, [&] { return extra.downs == 1; }));
    CHECK(extra.lastDown == "refused: the session is full");
    CHECK(session.PlayerCount() == players);
}

void TestRelaysWithoutTakingPart() {
    World w;
    RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));

    // Ann and Bob use game channels the server never registered. The library relays them anyway.
    Joiner ann(w.net, 2), bob(w.net, 3);
    ann.RegisterGame();
    bob.RegisterGame();
    bob.RegisterInfo();
    const Joiners all = { &ann, &bob };
    CHECK(JoinNow(w, session, all, ann, "Ann"));
    CHECK(JoinNow(w, session, all, bob, "Bob"));

    const uint8_t position[3] = { 1, 2, 3 };
    CHECK(ann.core.Send("game.pose", position, sizeof position) == sco::net::SendResult::Ok);
    CHECK(ann.core.Send("game.spawn", position, 2) == sco::net::SendResult::Ok);
    CHECK(RunUntil(w, session, all, 5000, [&] { return bob.Count("game.pose") == 1 && bob.Count("game.spawn") == 1; }));

    for (const Received& r : bob.got) {
        if (r.channel == "game.pose" || r.channel == "game.spawn") CHECK(r.from == 2);   // Ann's, relayed
        else CHECK(r.channel == "relay.info" && r.from == sco::net::kHostPeer);          // the server's only message
    }
    CHECK(ann.Count("game.pose") == 0 && ann.Count("game.spawn") == 0);   // not echoed back
}

void TestLeaveAndSilence() {
    World w;
    Settings s = TestSettings(3);
    s.keepaliveMs = 500;
    s.peerTimeoutMs = 2000;
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    Joiner ann(w.net, 2), bob(w.net, 3), cid(w.net, 4);
    const Joiners all = { &ann, &bob, &cid };
    CHECK(JoinNow(w, session, all, ann, "Ann"));
    CHECK(JoinNow(w, session, all, bob, "Bob"));
    CHECK(JoinNow(w, session, all, cid, "Cid"));
    CHECK(session.PlayerCount() == 3);

    // Bob leaves on purpose: the others are told, with his reason.
    bob.core.Leave("bye");
    CHECK(RunUntil(w, session, all, 5000, [&] { return ann.HasEvent("L3:left: bye") && cid.HasEvent("L3:left: bye"); }));
    CHECK(session.PlayerCount() == 2);
    CHECK(w.Logged("player 'Bob' (peer 3) left: bye"));

    // Cid goes silent (nobody pumps it any more): after the timeout the others are told. The
    // timeout is peer_timeout_ms from the config, not the library's 30 seconds.
    const uint64_t silentFrom = w.net.now;
    const Joiners awake = { &ann };
    CHECK(RunUntil(w, session, awake, 10000, [&] { return ann.HasEvent("L4:timed out"); }));
    CHECK(w.net.now >= silentFrom + 1400 && w.net.now <= silentFrom + 2600);
    CHECK(session.PlayerCount() == 1);
    CHECK(w.Logged("player 'Cid' (peer 4) timed out"));
}

// ---- relay.info after a late registration ------------------------------------------------------------

void TestRelayInfoForALateRegistration() {
    // A client can register relay.info after it joined, and the library tells the server nothing
    // about that. The server repeats the message, so the client still gets it within one interval.
    World w;
    Settings s = TestSettings(3);
    s.announceIntervalMs = 1000;
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));

    Joiner ann(w.net, 2);   // joins without registering relay.info
    const Joiners all = { &ann };
    CHECK(JoinNow(w, session, all, ann, "Ann"));
    RunUntil(w, session, all, 500, [] { return false; });
    CHECK(ann.Count("relay.info") == 0);

    ann.RegisterInfo();   // later, and nobody else joins
    CHECK(RunUntil(w, session, all, s.announceIntervalMs + 50, [&] { return ann.Count("relay.info") >= 1; }));
    CHECK(ann.got.size() >= 1);
    if (ann.got.empty()) return;
    sco::relay::RelayInfo info;
    CHECK(ann.got[0].from == sco::net::kHostPeer);
    CHECK(sco::relay::DecodeRelayInfo(ann.got[0].data.data(), ann.got[0].data.size(), &info) && info.maxPlayers == 3);

    // And it keeps coming, one per interval, for as long as the player is there.
    const size_t before = ann.Count("relay.info");
    CHECK(RunUntil(w, session, all, 2 * s.announceIntervalMs + 100, [&] { return ann.Count("relay.info") >= before + 2; }));
}

// ---- every timer in the config changes what the running server does -------------------------------------

// A joiner starts joining, the server answers, but the joiner hears nothing for 2 seconds (a slow
// network), then carries on. Says whether it got in, and how many handshake packets the server dropped.
bool JoinsAfterAStall(const Settings& s, uint64_t* handshakeDropped) {
    World w;
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    session.Start(0, &why);
    Joiner ann(w.net, 2);
    ann.Join(0, "Ann");
    RunUntil(w, session, Joiners(), 2000, [] { return false; });
    const bool up = RunUntil(w, session, { &ann }, 3000, [&] { return ann.Up(); });
    *handshakeDropped = session.CoreStats().handshakeDropped;
    return up;
}

void TestJoinTimeoutSetting() {
    uint64_t dropped = 0;
    Settings shortWait = TestSettings(3);
    shortWait.joinTimeoutMs = 1000;
    CHECK(!JoinsAfterAStall(shortWait, &dropped));   // the half-finished join was dropped after 1 s
    CHECK(dropped > 0);
    CHECK(JoinsAfterAStall(TestSettings(3), &dropped));   // the library's 10 s still waits for it
}

// Two joiners are in; the second stops answering. The first sends one reliable message that the
// server relays to the second, who never acknowledges it. Returns how often the server resent it
// in 4 seconds.
uint64_t ResendsInFourSeconds(uint32_t resendMs, uint32_t maxResendMs) {
    World w;
    Settings s = TestSettings(3);
    s.resendMs = resendMs;
    s.maxResendMs = maxResendMs;
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    session.Start(0, &why);
    Joiner ann(w.net, 2), bob(w.net, 3);
    ann.RegisterGame();
    bob.RegisterGame();
    const Joiners both = { &ann, &bob };
    JoinNow(w, session, both, ann, "Ann");
    JoinNow(w, session, both, bob, "Bob");
    const uint8_t spawn[2] = { 1, 2 };
    ann.core.Send("game.spawn", spawn, sizeof spawn);
    const uint64_t before = session.CoreStats().retransmits;
    RunUntil(w, session, { &ann }, 4000, [] { return false; });
    return session.CoreStats().retransmits - before;
}

void TestResendSettings() {
    // resend_ms: the first wait before a lost message is sent again.
    CHECK(ResendsInFourSeconds(100, 2000) >= ResendsInFourSeconds(800, 2000) + 2);
    // max_resend_ms: the waits double, up to this.
    CHECK(ResendsInFourSeconds(100, 200) >= ResendsInFourSeconds(100, 2000) + 5);
}

// How many datagrams the server sends in 6 quiet seconds with one player in the session.
uint64_t QuietDatagramsOut(uint32_t keepaliveMs) {
    World w;
    Settings s = TestSettings(3);
    s.keepaliveMs = keepaliveMs;
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    session.Start(0, &why);
    Joiner ann(w.net, 2);
    JoinNow(w, session, { &ann }, ann, "Ann");
    const uint64_t before = session.GetTraffic().datagramsOut;
    RunUntil(w, session, { &ann }, 6000, [] { return false; });
    return session.GetTraffic().datagramsOut - before;
}

void TestKeepaliveSetting() {
    // keepalive_ms: how soon the server pings a link that has gone quiet.
    CHECK(QuietDatagramsOut(100) >= QuietDatagramsOut(1000) + 20);
}

void TestRestart() {
    World w;
    RelaySession first(w.serverTransport, TestSettings(3), w.log, kTestIters);
    std::string why;
    CHECK(first.Start(0, &why));
    Joiner ann(w.net, 2);
    ann.RegisterInfo();
    const Joiners all = { &ann };
    CHECK(JoinNow(w, first, all, ann, "Ann"));

    // Stopping tells the players why, in words they read.
    first.Stop("restart please");
    CHECK(!first.Running());
    CHECK(first.PlayerCount() == 0);
    CHECK(RunUntil(w, first, all, 5000, [&] { return ann.downs == 1; }));
    CHECK(ann.lastDown == "the host ended the session: restart please");

    // A new server on the same address: the player joins again.
    RelaySession second(w.serverTransport, TestSettings(3), w.log, kTestIters);
    CHECK(second.Start(w.net.now, &why));
    ann.got.clear();
    CHECK(ann.Join(w.net.now, "Ann"));
    CHECK(RunUntil(w, second, all, 5000, [&] { return ann.Up() && ann.Count("relay.info") == 1; }));
    CHECK(second.PlayerCount() == 1);
}

void TestStartRefusals() {
    World w;
    std::string why;

    RelaySession tooBig(w.serverTransport, TestSettings(sco::relay::kSessionSeats), w.log, kTestIters);
    CHECK(!tooBig.Start(0, &why));
    CHECK(Contains(why, "Set max_players to"));
    CHECK(Contains(why, "allows at most"));   // its reason, not only its fix
    CHECK(!tooBig.Running());

    Settings noPassphrase = TestSettings(3);
    noPassphrase.passphrase.clear();
    RelaySession missing(w.serverTransport, noPassphrase, w.log, kTestIters);
    CHECK(!missing.Start(0, &why));
    CHECK(Contains(why, "passphrase"));

    RelaySession once(w.serverTransport, TestSettings(3), w.log, kTestIters);
    CHECK(once.Start(0, &why));
    CHECK(!once.Start(0, &why));
    CHECK(Contains(why, "already started"));
}

void TestAddressRule() {
    sco::net::Endpoint outside, vpn;
    CHECK(sco::net::ParseAddress("8.8.8.8", 5000, &outside));
    CHECK(sco::net::ParseAddress("100.64.1.2", 5000, &vpn));
    const std::vector<uint8_t> noise = { 1, 2, 3 };
    std::string why;

    // An address outside the rule is dropped before the library reads a byte.
    {
        World w;
        RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
        CHECK(session.Start(0, &why));
        w.net.Inject(outside, nettest::MemEndpoint(1), noise);
        w.net.Inject(vpn, nettest::MemEndpoint(1), noise);
        session.Pump(w.net.now);
        CHECK(session.DroppedByAddressRule() == 2);
        CHECK(session.GetTraffic().datagramsIn == 0);
    }
    // The same VPN range, once the config lists it, gets through; the outside address still does not.
    {
        World w;
        Settings s = TestSettings(3);
        CHECK(sco::relay::ParseAllowedAddresses("100.64.0.0/10", &s.allowedAddresses, &why));
        RelaySession session(w.serverTransport, s, w.log, kTestIters);
        CHECK(session.Start(0, &why));
        w.net.Inject(outside, nettest::MemEndpoint(1), noise);
        w.net.Inject(vpn, nettest::MemEndpoint(1), noise);
        session.Pump(w.net.now);
        CHECK(session.DroppedByAddressRule() == 1);
        CHECK(session.GetTraffic().datagramsIn == 1);
    }
}

void TestTrafficCounts() {
    World w;
    RelaySession session(w.serverTransport, TestSettings(3), w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    CHECK(session.GetTraffic().bytesIn == 0 && session.GetTraffic().bytesOut == 0);
    Joiner ann(w.net, 2);
    CHECK(JoinNow(w, session, { &ann }, ann, "Ann"));
    const sco::relay::Traffic& traffic = session.GetTraffic();
    CHECK(traffic.datagramsIn > 0 && traffic.bytesIn > 0);
    CHECK(traffic.datagramsOut > 0 && traffic.bytesOut > 0);
    CHECK(traffic.bytesIn >= traffic.datagramsIn && traffic.bytesOut >= traffic.datagramsOut);
}

void TestDefaultIterations() {
    // No iteration count given: the server uses the protocol's own, which a real player's client uses too.
    World w;
    RelaySession session(w.serverTransport, TestSettings(3), w.log);
    std::string why;
    CHECK(session.Start(0, &why));
    Joiner ann(w.net, 2);
    CHECK(ann.Join(0, "Ann", kPassphrase, sco::net::kPbkdf2Iters));
    CHECK(RunUntil(w, session, { &ann }, 5000, [&] { return ann.Up(); }));
    CHECK(session.PlayerCount() == 1);
}

// ---- two config files, two behaviours (no rebuild between them) ----------------------------------

bool WriteFile(const char* path, const std::string& text) {
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const bool wrote = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    return std::fclose(file) == 0 && wrote;
}

// Starts a server from the config file at `path`, lets three players try to join, and says how many got in.
int PlayersAdmitted(const char* path) {
    Settings s;
    std::string why;
    if (!sco::relay::LoadConfig(path, &s, &why)) return -1;
    World w;
    RelaySession session(w.serverTransport, s, w.log, kTestIters);
    if (!session.Start(0, &why)) return -1;
    Joiner a(w.net, 2), b(w.net, 3), c(w.net, 4);
    const Joiners all = { &a, &b, &c };
    a.Join(0, "A");
    b.Join(0, "B");
    c.Join(0, "C");
    RunUntil(w, session, all, 3000, [] { return false; });
    return static_cast<int>(session.PlayerCount());
}

void TestTwoConfigFiles() {
    CHECK(WriteFile("test-config-one.tmp", "passphrase = test passphrase\nmax_players = 1\n"));
    CHECK(WriteFile("test-config-two.tmp", "passphrase = test passphrase\nmax_players = 2\n"));
    CHECK(PlayersAdmitted("test-config-one.tmp") == 1);
    CHECK(PlayersAdmitted("test-config-two.tmp") == 2);
    std::remove("test-config-one.tmp");
    std::remove("test-config-two.tmp");
}

}  // namespace

namespace relaytest {

void RunSessionTests() {
    Run("TestJoinAndRelayInfo", TestJoinAndRelayInfo);
    Run("TestClientWithoutRelayInfo", TestClientWithoutRelayInfo);
    Run("TestWrongPassphrase", TestWrongPassphrase);
    Run("TestOnePlayerOverTheLimit", TestOnePlayerOverTheLimit);
    Run("TestLibraryLimit", TestLibraryLimit);
    Run("TestRelaysWithoutTakingPart", TestRelaysWithoutTakingPart);
    Run("TestLeaveAndSilence", TestLeaveAndSilence);
    Run("TestRelayInfoForALateRegistration", TestRelayInfoForALateRegistration);
    Run("TestJoinTimeoutSetting", TestJoinTimeoutSetting);
    Run("TestResendSettings", TestResendSettings);
    Run("TestKeepaliveSetting", TestKeepaliveSetting);
    Run("TestRestart", TestRestart);
    Run("TestStartRefusals", TestStartRefusals);
    Run("TestAddressRule", TestAddressRule);
    Run("TestTrafficCounts", TestTrafficCounts);
    Run("TestDefaultIterations", TestDefaultIterations);
    Run("TestTwoConfigFiles", TestTwoConfigFiles);
}

}  // namespace relaytest

// SPDX-License-Identifier: GPL-3.0-only
// Tests of the run loop and the stats line: the exact line format, one line per interval on a
// simulated clock, and the final line at shutdown.
#include "sco/net/scope.h"
#include "server/config.h"
#include "server/run_loop.h"
#include "server/session.h"
#include "server/tests/check.h"
#include "server/tests/session_kit.h"
#include <string>
#include <vector>

namespace {

using namespace relaytest;

std::vector<std::string> StatsLines(const World& w) {
    std::vector<std::string> lines;
    for (const std::string& line : w.lines)
        if (line.rfind("stats:", 0) == 0) lines.push_back(line);
    return lines;
}

void TestStatsLineFormat() {
    // This line is read by people and by scripts that measure the server: its shape is part of the contract.
    sco::relay::StatsReport r;
    r.players = 12;
    r.maxPlayers = 15;
    r.windowMs = 60000;
    r.bytesIn = 41233;
    r.datagramsIn = 410;
    r.bytesOut = 380512;
    r.datagramsOut = 3904;
    r.joinsRefused = 2;
    r.droppedByAddressRule = 7;
    r.rounds = 12000;
    r.roundTotalUs = 984000;   // an average of 82 microseconds
    r.roundMaxUs = 1912;
    CHECK(sco::relay::FormatStatsLine(r) ==
          "stats: players 12 of 15 | last 60.0 s: 41233 bytes in (410 datagrams), 380512 bytes out (3904 datagrams) | "
          "round time: average 0.082 ms, longest 1.912 ms over 12000 rounds | joins refused 2 | "
          "dropped by address rule 7");

    // No rounds yet: an average of zero, not a division by zero.
    CHECK(Contains(sco::relay::FormatStatsLine(sco::relay::StatsReport()), "average 0.000 ms"));
}

void TestStatsLineNearbyPart() {
    // With nearby-only on, the line ends with one more part; without it the line is the one above.
    sco::relay::StatsReport r;
    r.players = 3;
    r.maxPlayers = 15;
    r.windowMs = 1000;
    r.rounds = 1;
    r.nearbyOnly = true;
    r.nearby.sent = 120;
    r.nearby.sentWithoutPosition = 7;
    r.nearby.heldBack = 80;
    r.nearby.notSent = 2;
    r.nearby.badUp = 1;
    const std::string line = sco::relay::FormatStatsLine(r);
    CHECK(Contains(line, " | nearby: poses sent 120 (7 without the receiver's position), held back 80 (not near, or "
                         "position unknown for too long), not sent 2, bad position messages 1"));
    r.nearbyOnly = false;
    const std::string without = sco::relay::FormatStatsLine(r);
    CHECK(!Contains(without, "nearby"));
    CHECK(line.rfind(without, 0) == 0);   // the nearby part is added at the end, nothing before it moves
}

void TestStatsEveryInterval() {
    World w;
    const sco::relay::Settings s = TestSettings(3);   // a stats line every 1000 ms
    sco::relay::RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    sco::relay::RunLoop loop(session, s, w.log);
    CHECK(loop.WaitMs() == s.loopWaitMs);

    Joiner ann(w.net, 2), bob(w.net, 3);
    CHECK(ann.Join(0, "Ann"));
    CHECK(bob.Join(0, "Bob"));

    // 3.1 simulated seconds, driven by the loop: a line at 1, 2 and 3 seconds, and no other.
    for (uint64_t t = 0; t <= 3100; t += 5) {
        w.net.now = t;
        loop.Pass(t);
        ann.core.Pump(t);
        bob.core.Pump(t);
    }
    const std::vector<std::string> lines = StatsLines(w);
    CHECK(lines.size() == 3);
    if (lines.size() != 3) return;
    CHECK(Contains(lines[0], "stats: players 2 of 3 | last 1.0 s:"));
    CHECK(Contains(lines[0], "round time: average") && Contains(lines[0], "longest"));
    CHECK(Contains(lines[2], "players 2 of 3"));

    // The program asks for one more at shutdown, for the time since the last line.
    loop.ReportStats(3100);
    CHECK(StatsLines(w).size() == 4);
    CHECK(Contains(StatsLines(w)[3], "last 0.1 s:"));
}

void TestWarnsAboutDroppedSources() {
    World w;
    const sco::relay::Settings s = TestSettings(3);   // a stats line every 1000 ms
    sco::relay::RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    sco::relay::RunLoop loop(session, s, w.log);

    sco::net::Endpoint first, second;
    CHECK(sco::net::ParseAddress("8.8.8.8", 5000, &first));
    CHECK(sco::net::ParseAddress("9.9.9.9", 6000, &second));
    const std::vector<uint8_t> noise = { 1, 2, 3 };
    const auto runTo = [&](uint64_t untilMs) {
        for (uint64_t t = w.net.now; t <= untilMs; t += 5) {
            w.net.now = t;
            loop.Pass(t);
        }
    };

    // Three datagrams from outside the address rule in the first interval, the last from 9.9.9.9.
    runTo(0);
    w.net.Inject(first, nettest::MemEndpoint(1), noise);
    w.net.Inject(first, nettest::MemEndpoint(1), noise);
    w.net.Inject(second, nettest::MemEndpoint(1), noise);
    runTo(1100);
    const sco::relay::LogLevel warn = sco::relay::LogLevel::Warn;
    CHECK(w.CountAt(warn, "dropped datagrams from outside the allowed addresses") == 1);
    CHECK(w.CountAt(warn, "3 in the last 1.0 s, the latest from 9.9.9.9.") == 1);   // the address, not its port
    CHECK(w.CountAt(warn, "add the player's range to allowed_addresses") == 1);
    CHECK(w.Logged("dropped by address rule 3"));   // the stats line counts them too

    // A quiet interval gives no warning; a new burst gives one more, never one per datagram.
    runTo(2100);
    CHECK(w.CountAt(warn, "dropped datagrams") == 1);
    w.net.Inject(first, nettest::MemEndpoint(1), noise);
    w.net.Inject(first, nettest::MemEndpoint(1), noise);
    runTo(3100);
    CHECK(w.CountAt(warn, "dropped datagrams") == 2);
    CHECK(w.CountAt(warn, "2 in the last 1.0 s, the latest from 8.8.8.8.") == 1);
}

void TestStopBeforeTheFirstPass() {
    // A stop during start-up reports an empty window, not the seconds since the machine booted.
    World w;
    const sco::relay::Settings s = TestSettings(3);
    sco::relay::RelaySession session(w.serverTransport, s, w.log, kTestIters);
    std::string why;
    CHECK(session.Start(0, &why));
    sco::relay::RunLoop loop(session, s, w.log);
    loop.ReportStats(123456789);
    CHECK(w.Logged("stats: players 0 of 3 | last 0.0 s:"));
}

void TestEveryLogLevelDiffers() {
    // Each level shows what its name says and the levels before it, so no two levels behave alike.
    using sco::relay::LogLevel;
    const LogLevel levels[4] = { LogLevel::Error, LogLevel::Warn, LogLevel::Info, LogLevel::Debug };
    for (size_t i = 0; i < 4; ++i) {
        size_t shown = 0;
        sco::relay::Logger log(levels[i], [&shown](LogLevel, const std::string&) { ++shown; });
        log.Log(LogLevel::Error, "an error");
        log.Log(LogLevel::Warn, "a warning");
        log.Log(LogLevel::Info, "some information");
        log.Log(LogLevel::Debug, "a detail");
        CHECK(shown == i + 1);
    }
}

}  // namespace

namespace relaytest {

void RunLoopTests() {
    Run("TestWarnsAboutDroppedSources", TestWarnsAboutDroppedSources);
    Run("TestStopBeforeTheFirstPass", TestStopBeforeTheFirstPass);
    Run("TestEveryLogLevelDiffers", TestEveryLogLevelDiffers);
    Run("TestStatsLineFormat", TestStatsLineFormat);
    Run("TestStatsLineNearbyPart", TestStatsLineNearbyPart);
    Run("TestStatsEveryInterval", TestStatsEveryInterval);
}

}  // namespace relaytest

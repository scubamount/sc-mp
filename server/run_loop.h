// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// RunLoop: the pacing around a RelaySession. Each Pass pumps the session once, times that pass,
// and prints the stats line when the interval is up. It does not own a clock or a socket: the
// caller says what time it is and does the waiting between passes, so the program can run it on
// the real clock and a test can run the same code on a simulated one.
//
// The stats line is how an operator sees the server is healthy: players now, bytes and datagrams
// in and out since the last line, and the round time, which is how long one pass took (the
// longest and the average since the last line). The round time is always real elapsed time, even
// when the caller's clock is simulated, so a simulated run still measures what the server costs.
// When the address rule dropped datagrams since the last line, one Warn line follows it with the
// count, the latest source address and the fix. With nearby_only on, the line ends with a nearby
// part (poses sent, held back because the receiver is not near or has been without a position for too
// long, sent without the receiver's position), and one Warn line follows
// when position messages were not valid.
#include "server/config.h"
#include "server/log.h"
#include "server/nearby.h"
#include "server/session.h"
#include "server/traffic.h"
#include <cstdint>
#include <string>

namespace sco::relay {

// Everything one stats line says. Counts are for the time since the previous line.
struct StatsReport {
    uint32_t players = 0;
    uint32_t maxPlayers = 0;
    uint64_t windowMs = 0;
    uint64_t bytesIn = 0;
    uint64_t datagramsIn = 0;
    uint64_t bytesOut = 0;
    uint64_t datagramsOut = 0;
    uint64_t joinsRefused = 0;
    uint64_t droppedByAddressRule = 0;
    uint64_t rounds = 0;
    uint64_t roundTotalUs = 0;
    uint64_t roundMaxUs = 0;
    bool         nearbyOnly = false;   // when true the line ends with the nearby part
    NearbyCounts nearby;               // since the previous line
};

// "stats: players 12 of 15 | last 60.0 s: 41233 bytes in (410 datagrams), ..." on one line.
std::string FormatStatsLine(const StatsReport& report);

class RunLoop {
public:
    RunLoop(RelaySession& session, const Settings& settings, Logger& log);

    // One pass at the caller's time `nowMs`: pump, time it, print the stats line when it is due.
    void Pass(uint64_t nowMs);
    // How long the caller may wait before the next pass (loop_wait_ms). A datagram arriving ends
    // the wait early when the caller waits on its socket.
    uint32_t WaitMs() const { return waitMs_; }
    // Prints the stats line for the time since the last one, now. Pass does it when the interval
    // is up; the program does it once more at shutdown.
    void ReportStats(uint64_t nowMs);

private:
    void WarnAboutDroppedSources(const StatsReport& report);
    void WarnAboutBadPositions(const StatsReport& report);

    RelaySession& session_;
    Logger&       log_;
    uint32_t      waitMs_;
    uint32_t      statsIntervalMs_;

    bool     started_ = false;
    uint64_t windowStartMs_ = 0;
    Traffic  lastTraffic_;
    uint64_t lastJoinsRefused_ = 0;
    uint64_t lastDropped_ = 0;
    NearbyCounts lastNearby_;
    uint64_t rounds_ = 0;
    uint64_t roundTotalUs_ = 0;
    uint64_t roundMaxUs_ = 0;
};

}  // namespace sco::relay

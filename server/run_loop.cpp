// SPDX-License-Identifier: GPL-3.0-only
// RunLoop and the stats line (server/run_loop.h): one pass is one pump of the session, timed with
// the real clock; the stats line is written when the interval is up.
#include "server/run_loop.h"
#include "server/address_rule.h"
#include <chrono>

namespace sco::relay {

namespace {

// The nearby part of the stats line, with the leading " | ".
std::string FormatNearbyPart(const NearbyCounts& n) {
    return Format(" | nearby: poses sent %llu (%llu without the receiver's position), held back %llu (not near, or "
                  "position unknown for too long), not sent %llu, bad position messages %llu",
                  Ull(n.sent), Ull(n.sentWithoutPosition), Ull(n.heldBack), Ull(n.notSent), Ull(n.badUp));
}

NearbyCounts Since(const NearbyCounts& now, const NearbyCounts& before) {
    NearbyCounts n;
    n.sent = now.sent - before.sent;
    n.sentWithoutPosition = now.sentWithoutPosition - before.sentWithoutPosition;
    n.heldBack = now.heldBack - before.heldBack;
    n.notSent = now.notSent - before.notSent;
    n.badUp = now.badUp - before.badUp;
    return n;
}

}  // namespace

std::string FormatStatsLine(const StatsReport& r) {
    const double windowS = static_cast<double>(r.windowMs) / 1000.0;
    const double averageMs =
        r.rounds ? static_cast<double>(r.roundTotalUs) / static_cast<double>(r.rounds) / 1000.0 : 0.0;
    const double longestMs = static_cast<double>(r.roundMaxUs) / 1000.0;
    std::string line =
        Format("stats: players %u of %u | last %.1f s: %llu bytes in (%llu datagrams), "
               "%llu bytes out (%llu datagrams) | round time: average %.3f ms, longest %.3f ms over %llu rounds | "
               "joins refused %llu | dropped by address rule %llu",
               r.players, r.maxPlayers, windowS, Ull(r.bytesIn), Ull(r.datagramsIn), Ull(r.bytesOut),
               Ull(r.datagramsOut), averageMs, longestMs, Ull(r.rounds), Ull(r.joinsRefused),
               Ull(r.droppedByAddressRule));
    if (r.nearbyOnly) line += FormatNearbyPart(r.nearby);
    return line;
}

// One line per report at most, and only when something was dropped: those datagrams came from
// addresses that are not in the session, so this is the only place an operator can learn which
// range a player needs.
void RunLoop::WarnAboutDroppedSources(const StatsReport& report) {
    Address source;
    const std::string from = session_.LastRejectedSource(&source) ? FormatSourceAddress(source) : "an unknown address";
    log_.Log(LogLevel::Warn,
             "dropped datagrams from outside the allowed addresses: %llu in the last %.1f s, the latest from %s. "
             "If that is a player, add the player's range to allowed_addresses in the config file and restart the "
             "server.",
             Ull(report.droppedByAddressRule), static_cast<double>(report.windowMs) / 1000.0, from.c_str());
}

// The server cannot say which player sent a bad position message, and the players' mod is the usual
// cause, so the line says what to check.
void RunLoop::WarnAboutBadPositions(const StatsReport& report) {
    log_.Log(LogLevel::Warn,
             "dropped %llu nearby position messages in the last %.1f s that were not valid, or came from a player "
             "who is not in the session. If a player sees nobody, check that every player runs the same multiplayer "
             "plugin build, or set nearby_only to off.",
             Ull(report.nearby.badUp), static_cast<double>(report.windowMs) / 1000.0);
}

RunLoop::RunLoop(RelaySession& session, const Settings& settings, Logger& log)
    : session_(session), log_(log), waitMs_(settings.loopWaitMs), statsIntervalMs_(settings.statsIntervalMs) {}

void RunLoop::Pass(uint64_t nowMs) {
    if (!started_) {
        started_ = true;
        windowStartMs_ = nowMs;
    }
    const auto begin = std::chrono::steady_clock::now();
    session_.Pump(nowMs);
    const auto end = std::chrono::steady_clock::now();

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
    const uint64_t us = static_cast<uint64_t>(elapsed.count());
    ++rounds_;
    roundTotalUs_ += us;
    if (us > roundMaxUs_) roundMaxUs_ = us;

    if (nowMs >= windowStartMs_ + statsIntervalMs_) ReportStats(nowMs);
}

void RunLoop::ReportStats(uint64_t nowMs) {
    const Traffic& traffic = session_.GetTraffic();
    const uint64_t joinsRefused = session_.CoreStats().joinsRefused;
    const uint64_t dropped = session_.DroppedByAddressRule();
    const NearbyCounts nearby = session_.NearbyStats();

    // A stop before the first pass has no window yet: nothing happened, so the window is empty.
    const uint64_t windowStartMs = started_ ? windowStartMs_ : nowMs;

    StatsReport report;
    report.players = session_.PlayerCount();
    report.maxPlayers = session_.MaxPlayers();
    report.windowMs = nowMs > windowStartMs ? nowMs - windowStartMs : 0;
    report.bytesIn = traffic.bytesIn - lastTraffic_.bytesIn;
    report.datagramsIn = traffic.datagramsIn - lastTraffic_.datagramsIn;
    report.bytesOut = traffic.bytesOut - lastTraffic_.bytesOut;
    report.datagramsOut = traffic.datagramsOut - lastTraffic_.datagramsOut;
    report.joinsRefused = joinsRefused - lastJoinsRefused_;
    report.droppedByAddressRule = dropped - lastDropped_;
    report.rounds = rounds_;
    report.roundTotalUs = roundTotalUs_;
    report.roundMaxUs = roundMaxUs_;
    report.nearbyOnly = session_.NearbyOnly();
    report.nearby = Since(nearby, lastNearby_);
    log_.Log(LogLevel::Info, "%s", FormatStatsLine(report).c_str());
    if (report.droppedByAddressRule > 0) WarnAboutDroppedSources(report);
    if (report.nearby.badUp > 0) WarnAboutBadPositions(report);

    windowStartMs_ = nowMs;
    lastTraffic_ = traffic;
    lastJoinsRefused_ = joinsRefused;
    lastDropped_ = dropped;
    lastNearby_ = nearby;
    rounds_ = 0;
    roundTotalUs_ = 0;
    roundMaxUs_ = 0;
}

}  // namespace sco::relay

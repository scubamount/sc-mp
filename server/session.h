// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// RelaySession: the server's one real job. It hosts a sco.net session that players join, and the
// network library relays what the players send to each other. The server is not a player: it
// registers no player channel and reads no pose. What it sends is three kinds of message of its own:
//   - relay.info (server/relay_info.h): peer 1 is a server, and how many players it takes;
//   - multiplayer.relay (plugins/multiplayer/relay_wire.h): who is in, in which slot;
//   - with nearby_only on, a copy of each pose to the players near the sender, on the channel of
//     each receiver's slot (server/nearby.h).
// The first two go out when a player joins and every announce_interval_ms after, because the library
// tells the server nothing when a player registers a channel after joining.
//
// Seams for the callers. The session takes its pipe (DatagramTransport) and the time from the caller:
//   - the program gives it a real UDP socket and a real clock (server/main.cpp);
//   - the tests and the rig can give it an in-memory wire and a simulated clock, and run this very code.
// The address rule (allowed_addresses) is applied here, around whatever pipe is given, so it holds the
// same way on both. In-memory addresses always pass it.
//
// Players are not counted by a constant: the limit comes from Settings::maxPlayers. The session
// itself takes peer 1 of the library's places, so the library gets maxPlayers + 1.
#include "server/config.h"
#include "server/log.h"
#include "server/nearby.h"
#include "server/net_boundary.h"
#include "server/roster.h"
#include "server/traffic.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sco::relay {

class RelaySession {
public:
    // `transport` and `log` must outlive the session. pbkdf2Iters is for tests that need speed; it
    // is not a setting, and the default is the protocol's value, which real players use.
    RelaySession(DatagramTransport& transport, const Settings& settings, Logger& log,
                 uint32_t pbkdf2Iters = GetLibraryDefaults().pbkdf2Iters);
    RelaySession(const RelaySession&) = delete;
    RelaySession& operator=(const RelaySession&) = delete;

    // Starts hosting. false, with *why naming the fix, when the settings are refused. A session
    // starts once; to restart, make a new RelaySession.
    bool Start(uint64_t nowMs, std::string* why);
    // Receives what is waiting, runs the timers and repeats relay.info and multiplayer.relay when
    // due. Call it often, with the caller's clock.
    void Pump(uint64_t nowMs);
    // Tells every player the session is over, with a reason they will read, and ends it.
    void Stop(std::string_view reason);

    bool Running() const { return hosted_.Hosting(); }
    uint32_t PlayerCount() const { return roster_.Count(); }
    uint32_t MaxPlayers() const { return settings_.maxPlayers; }
    const Roster& GetRoster() const { return roster_; }

    // Totals since Start, for the stats line and for measurements.
    const Traffic& GetTraffic() const { return counted_.Totals(); }
    uint64_t DroppedByAddressRule() const { return filter_.Dropped(); }
    // Where the latest datagram dropped by the address rule came from; false if none was.
    bool LastRejectedSource(Address* out) const { return filter_.LastRejected(out); }
    SessionStats CoreStats() const { return hosted_.Stats(); }
    bool NearbyOnly() const { return settings_.nearbyOnly; }
    const NearbyCounts& NearbyStats() const { return nearby_.Counts(); }

private:
    SessionEvents MakeEvents();
    bool AddChannels(std::string* why);
    bool AddChannel(std::string_view name, ChannelMode mode, uint32_t maxBytes, std::string* why);
    void LogNearbyMode();
    void OnPlayerJoined(PeerId id, std::string_view name);
    void OnPlayerLeft(PeerId id, std::string_view how);
    void OnStateChanged(bool active, std::string_view reason);
    void OnMessage(std::string_view channel, PeerId from, const uint8_t* data, uint32_t len);
    void Announce();
    void SendRelayInfo();
    void SendRoster();

    Settings                      settings_;
    Logger&                       log_;
    uint32_t                      pbkdf2Iters_;
    AddressFilter                 filter_;    // the address rule, in front of everything
    CountingTransport             counted_;   // what passed the rule
    Roster                        roster_;    // the players now in the session, and their slots
    NearbyForwarder               nearby_;
    NearbyForwarder::SendCopy     sendCopy_;
    std::vector<std::string>      nearChannels_;   // multiplayer.near.<slot>, one per slot; filled by Start
    HostedSession                 hosted_;
    uint64_t                      nowMs_ = 0;
    uint64_t                      nextAnnounceMs_ = 0;
};

}  // namespace sco::relay

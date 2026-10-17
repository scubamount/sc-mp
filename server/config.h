// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The server's settings and the reader of the one config file they come from. Every tunable lives
// in that file, so changing one never needs a rebuild. The file is plain text:
//
//     # a comment line (a # anywhere else is part of the value)
//     name = value
//
// Spaces around the name and the value are ignored, and so are Windows line endings and a UTF-8
// byte-order mark at the start. The reader is strict on purpose: an unknown name, a missing or
// unusable passphrase, a player limit above the library's, a port players' games cannot join, a
// bad address range or a setting given twice stops the server, with a message that says the fix.
// A fault in one line names that line. A missing setting, or settings that disagree with each
// other, name the settings instead, because no one line is at fault.
//
// Only settings that change what a running server does are listed. The password stretching count
// (PBKDF2) is not one of them: players cannot change it, so the server must not either; it stays
// at the protocol's value.
#include "server/log.h"
#include "server/net_boundary.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sco::relay {

inline constexpr uint16_t kDefaultPort = 64091;   // the port sco-core documents for a session
// The players' side limits, so the server never accepts what no player could type or join.
inline constexpr uint16_t kLowestPort = 1024;          // the game refuses to join a lower port
inline constexpr uint32_t kMinPassphraseBytes = 12;    // the game's private-session host control wants at least this many
inline constexpr uint32_t kMaxPassphraseBytes = 128;   // and the game keeps at most this many

// Nearby-only forwarding (server/nearby.h). The default distance is above the 10 km the plugin shows
// players at, so a player never sees another pop in at the edge. The stale time is how long a
// player's last position counts; the plugin sends one every 100 ms, so 500 ms is five lost in a row.
inline constexpr uint32_t kDefaultNearbyDistanceM = 12000;
inline constexpr uint32_t kMaxNearbyDistanceM = 1000000000;   // a million kilometres; a longer one is a typo
inline constexpr uint32_t kDefaultNearbyStaleMs = 2000;
inline constexpr uint32_t kMinNearbyStaleMs = 500;
// The example config file's passphrase starts with this. Anyone can read it there, so the server
// refuses to start with it.
inline constexpr const char* kPlaceholderPassphrase = "CHANGE-ME";

struct Settings {
    Settings();   // the timers start at the network library's own defaults

    uint16_t    port = kDefaultPort;
    std::string passphrase;                  // required
    uint32_t    maxPlayers = 0;              // required; players only, the server is not counted
    std::string serverName = "sco-relay";    // what players see for peer 1
    std::vector<AddressRange> allowedAddresses;   // IPv4 ranges on top of the private ones

    // The network library's timers, all in milliseconds (see HostRequest in net_boundary.h). The library's
    // retry timer is left out: only a joining player uses it, never the host.
    uint32_t joinTimeoutMs = 0;
    uint32_t resendMs = 0;
    uint32_t maxResendMs = 0;
    uint32_t keepaliveMs = 0;
    uint32_t peerTimeoutMs = 0;

    uint32_t loopWaitMs = 5;             // the longest the run loop sleeps between passes
    uint32_t announceIntervalMs = 5000;  // how often relay.info is repeated to the players
    uint32_t statsIntervalMs = 60000;    // time between two stats lines
    LogLevel logLevel = LogLevel::Info;

    // Nearby-only forwarding: off, every player's pose goes to every other player, as the library relays it.
    bool     nearbyOnly = false;
    uint32_t nearbyDistanceM = kDefaultNearbyDistanceM;   // metres between two players in the same area
    uint32_t nearbyStaleMs = kDefaultNearbyStaleMs;       // how old a player's position may be
};

// The names a config file may use, in the order the example file lists them.
std::vector<std::string> SettingNames();

// Reads config text. On success *out holds the settings; on failure *why holds the message and
// *out is untouched.
bool ParseConfig(std::string_view text, Settings* out, std::string* why);

// Reads a config file from disk (at most 64 KiB), then ParseConfig. The message starts with the path.
bool LoadConfig(const std::string& path, Settings* out, std::string* why);

// Checks the whole of a Settings the way ParseConfig does at its end: the two required settings
// are there and usable and the settings agree with each other. RelaySession::Start runs it too,
// so a program that builds Settings by hand gets the same refusals.
bool CheckSettings(const Settings& settings, std::string* why);

// The session holds kSessionSeats peers (net_boundary.h) and the server is one of them. Writes the
// message that names the fix when maxPlayers is too big.
bool CheckPlayerLimit(uint32_t maxPlayers, std::string* why);

}  // namespace sco::relay

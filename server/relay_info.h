// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// relay.info: the one message of our own. It tells a client that peer 1 is a relay server and not
// a player, and how many players the server accepts. Everything else a client sends or receives is
// the clients' own business: the server relays it and never reads it.
//
// Channel   "relay.info", reliable, FROM_HOST (only peer 1, the server, can send it).
// Sent      by the server right after each player joins, and again every announce_interval_ms
//           (5 seconds unless the config file says otherwise) while at least one player is in the
//           session, each time to every player that has registered the channel by then. A client
//           that put the channel name in its join request gets it right after joining. Any other
//           client gets it within one interval: the name did not fit in the request, or the client
//           registered it later (the library tells the server nothing about that). The same
//           message arrives more than once; read it as "still true".
//           A client that never registers the channel is not affected: nothing is sent to it.
// Layout    version 1, 3 bytes, little-endian:
//             offset 0   u8    version      1
//             offset 1   u16   maxPlayers   how many players the server accepts; the server itself
//                                           is not counted
// Rules     A reader looks at the version byte first and ignores a message whose version it does
//           not know, or whose length is not the one that version has. A new layout gets a new
//           version number; version 1 never changes.
#include <cstddef>
#include <cstdint>

namespace sco::relay {

inline constexpr const char* kRelayInfoChannel = "relay.info";
inline constexpr uint8_t  kRelayInfoVersion = 1;
inline constexpr uint32_t kRelayInfoBytes = 3;

struct RelayInfo {
    uint16_t maxPlayers = 0;
};

void EncodeRelayInfo(const RelayInfo& info, uint8_t out[kRelayInfoBytes]);
// false when data is not exactly a version 1 message.
bool DecodeRelayInfo(const uint8_t* data, size_t len, RelayInfo* out);

}  // namespace sco::relay

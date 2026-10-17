// SPDX-License-Identifier: GPL-3.0-only
// The relay.info message (server/relay_info.h): writing and reading its bytes.
// The layout is documented in the header, which is what a client is built against.
#include "server/relay_info.h"

namespace sco::relay {

void EncodeRelayInfo(const RelayInfo& info, uint8_t out[kRelayInfoBytes]) {
    out[0] = kRelayInfoVersion;
    out[1] = static_cast<uint8_t>(info.maxPlayers & 0xFF);
    out[2] = static_cast<uint8_t>(info.maxPlayers >> 8);
}

bool DecodeRelayInfo(const uint8_t* data, size_t len, RelayInfo* out) {
    if (!data || !out || len != kRelayInfoBytes || data[0] != kRelayInfoVersion) return false;
    out->maxPlayers = static_cast<uint16_t>(data[1] | (data[2] << 8));
    return true;
}

}  // namespace sco::relay

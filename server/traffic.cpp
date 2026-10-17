// SPDX-License-Identifier: GPL-3.0-only
// Traffic counting around a pipe (server/traffic.h): every datagram that goes through is
// counted, and nothing else about it is touched.
#include "server/traffic.h"

namespace sco::relay {

bool CountingTransport::Send(const Address& to, const uint8_t* data, size_t len) {
    const bool sent = inner_.Send(to, data, len);
    if (sent) {
        ++totals_.datagramsOut;
        totals_.bytesOut += len;
    }
    return sent;
}

bool CountingTransport::Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) {
    if (!inner_.Receive(from, buf, cap, len)) return false;
    ++totals_.datagramsIn;
    totals_.bytesIn += *len;
    return true;
}

}  // namespace sco::relay

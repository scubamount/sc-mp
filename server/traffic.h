// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Counts the datagrams and bytes that cross a pipe, so the stats line can say how much the server
// sends and receives. The network library's own counters count datagrams only, not bytes.
#include "server/net_boundary.h"
#include <cstddef>
#include <cstdint>

namespace sco::relay {

struct Traffic {
    uint64_t datagramsIn = 0;
    uint64_t bytesIn = 0;
    uint64_t datagramsOut = 0;
    uint64_t bytesOut = 0;
};

// Passes everything through to `inner`. A datagram the inner pipe refused to send is not counted.
class CountingTransport final : public DatagramTransport {
public:
    explicit CountingTransport(DatagramTransport& inner) : inner_(inner) {}

    bool Send(const Address& to, const uint8_t* data, size_t len) override;
    bool Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) override;

    const Traffic& Totals() const { return totals_; }

private:
    DatagramTransport& inner_;
    Traffic            totals_;
};

}  // namespace sco::relay

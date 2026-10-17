// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The server's end of sco-core's in-memory test network, as the pipe a RelaySession takes. The
// server speaks the Address of server/net_boundary.h; sco-core's MemTransport speaks its own
// Endpoint. This translates, field by field. Test code may name the library; the server's own code
// may not, which is the point of the boundary.
#include "net_mem.h"
#include "server/net_boundary.h"
#include <cstdint>
#include <cstring>

namespace relaytest {

inline sco::net::Endpoint ToEndpoint(const sco::relay::Address& address) {
    sco::net::Endpoint endpoint;
    endpoint.family = address.family;
    endpoint.port = address.port;
    std::memcpy(endpoint.addr, address.bytes, sizeof endpoint.addr);
    return endpoint;
}

inline sco::relay::Address ToAddress(const sco::net::Endpoint& endpoint) {
    sco::relay::Address address;
    address.family = endpoint.family;
    address.port = endpoint.port;
    std::memcpy(address.bytes, endpoint.addr, sizeof address.bytes);
    return address;
}

class MemoryEnd final : public sco::relay::DatagramTransport {
public:
    explicit MemoryEnd(nettest::MemTransport& inner) : inner_(inner) {}

    bool Send(const sco::relay::Address& to, const uint8_t* data, size_t len) override {
        return inner_.Send(ToEndpoint(to), data, len);
    }
    bool Receive(sco::relay::Address* from, uint8_t* buf, size_t cap, size_t* len) override {
        sco::net::Endpoint source;
        if (!inner_.Receive(&source, buf, cap, len)) return false;
        *from = ToAddress(source);
        return true;
    }

private:
    nettest::MemTransport& inner_;
};

}  // namespace relaytest

// SPDX-License-Identifier: GPL-3.0-only
// Test that the built-in address ranges the server writes in its log (server/address_rule.cpp) are the
// ones sco-core's own rule lets in (IsPrivateAddress in server/net_boundary.h, which asks the library).
// The log text is a hand copy: sco-core keeps no list to read, only the function. So this test reads
// the text back into ranges and compares it with IsPrivateAddress at the first and last address of
// every range, the address just outside each, and every /16 block of IPv4. A range that moves in
// sco-core, or is added there, fails it. IPv6 is left out: the server is IPv4 only.
#include "server/address_rule.h"
#include "server/net_boundary.h"
#include "server/tests/check.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using sco::relay::Address;
using sco::relay::AddressRange;

uint32_t FirstAddress(const AddressRange& range) {
    return (static_cast<uint32_t>(range.bytes[0]) << 24) | (static_cast<uint32_t>(range.bytes[1]) << 16) |
           (static_cast<uint32_t>(range.bytes[2]) << 8) | static_cast<uint32_t>(range.bytes[3]);
}

uint32_t LastAddress(const AddressRange& range) {
    return FirstAddress(range) | (range.bits >= 32 ? 0u : (0xFFFFFFFFu >> range.bits));
}

Address AddressOf(uint32_t value) {
    Address address;
    address.family = Address::kIpv4;
    address.port = 5000;
    address.bytes[0] = static_cast<uint8_t>(value >> 24);
    address.bytes[1] = static_cast<uint8_t>(value >> 16);
    address.bytes[2] = static_cast<uint8_t>(value >> 8);
    address.bytes[3] = static_cast<uint8_t>(value);
    return address;
}

bool Listed(const std::vector<AddressRange>& ranges, const Address& address) {
    for (const AddressRange& range : ranges)
        if (sco::relay::InAddressRange(range, address)) return true;
    return false;
}

// true when the log text and the library give the same answer for this address; says which address when not.
bool Agree(const std::vector<AddressRange>& ranges, uint32_t value) {
    const Address address = AddressOf(value);
    if (sco::relay::IsPrivateAddress(address) == Listed(ranges, address)) return true;
    std::printf("  the log text and the library disagree about %u.%u.%u.%u\n", static_cast<unsigned>(address.bytes[0]),
                static_cast<unsigned>(address.bytes[1]), static_cast<unsigned>(address.bytes[2]),
                static_cast<unsigned>(address.bytes[3]));
    return false;
}

void TestBuiltInRangesMatchTheLibrary() {
    std::vector<AddressRange> ranges;
    std::string why;
    CHECK(sco::relay::ParseAllowedAddresses(sco::relay::DescribeAddressRule({}), &ranges, &why));
    CHECK(ranges.size() == 5);   // loopback, 10/8, 172.16/12, 192.168/16, link-local

    for (const AddressRange& range : ranges) {
        const uint32_t first = FirstAddress(range);
        const uint32_t last = LastAddress(range);
        CHECK(sco::relay::IsPrivateAddress(AddressOf(first)) && sco::relay::IsPrivateAddress(AddressOf(last)));   // both edges are inside
        if (first > 0) CHECK(Agree(ranges, first - 1));                                  // the neighbours are
        if (last < 0xFFFFFFFFu) CHECK(Agree(ranges, last + 1));                          // inside only when another range holds them
    }

    // A range the text does not list, or one it lists that the library does not allow, shows up on some /16 block.
    for (uint32_t block = 0; block < 65536; ++block) {
        CHECK(Agree(ranges, block << 16));
        CHECK(Agree(ranges, (block << 16) | 0xFFFFu));
    }
}

Address Ipv4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return AddressOf((a << 24) | (b << 16) | (c << 8) | d); }

// A pipe that hands the filter datagrams from the addresses it is given, and remembers where the filter sent.
class ScriptedPipe final : public sco::relay::DatagramTransport {
public:
    bool Send(const Address& to, const uint8_t*, size_t) override {
        sentTo.push_back(to);
        return true;
    }
    bool Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) override {
        if (next >= incoming.size()) return false;
        *from = incoming[next++];
        if (cap > 0) buf[0] = 7;
        *len = 1;
        return true;
    }

    std::vector<Address> incoming;
    size_t               next = 0;
    std::vector<Address> sentTo;
};

std::vector<Address> Drain(sco::relay::AddressFilter& filter) {
    std::vector<Address> got;
    uint8_t buffer[16];
    size_t len = 0;
    Address from;
    while (filter.Receive(&from, buffer, sizeof buffer, &len)) got.push_back(from);
    return got;
}

// The rule is the private ranges plus the extra ones and nothing more: the library's switches that would
// let every address in (any), turn the private ranges off (allowOnly) or cap what a player may send
// (maxDatagramsPerSecond) stay at their defaults. The old test asserted that on the library's Scope; the
// Scope is the boundary's now, so this asserts what it does.
void TestTheFilterLetsInTheRuleAndNothingElse() {
    std::vector<AddressRange> extra;
    std::string why;
    CHECK(sco::relay::ParseAllowedAddresses("100.64.0.0/10", &extra, &why));
    ScriptedPipe pipe;
    pipe.incoming = { Ipv4(8, 8, 8, 8), Ipv4(10, 1, 2, 3), Ipv4(100, 64, 1, 2), Ipv4(203, 0, 113, 9), Ipv4(127, 0, 0, 1) };
    sco::relay::AddressFilter filter(pipe, extra);

    const std::vector<Address> got = Drain(filter);
    CHECK(got.size() == 3);
    if (got.size() == 3) CHECK(got[0] == Ipv4(10, 1, 2, 3) && got[1] == Ipv4(100, 64, 1, 2) && got[2] == Ipv4(127, 0, 0, 1));
    CHECK(filter.Dropped() == 2);   // not "any": the two public addresses stayed out
    Address lastRejected;
    CHECK(filter.LastRejected(&lastRejected) && lastRejected == Ipv4(203, 0, 113, 9));

    // Sends obey the same rule.
    const uint8_t byte = 1;
    CHECK(!filter.Send(Ipv4(8, 8, 8, 8), &byte, 1) && pipe.sentTo.empty());
    CHECK(filter.Send(Ipv4(192, 168, 1, 5), &byte, 1) && filter.Send(Ipv4(100, 64, 9, 9), &byte, 1) && pipe.sentTo.size() == 2);

    // Not "allowOnly" (loopback and the private ranges got in above), and no cap on what players send:
    // 500 datagrams in a row from one player all arrive.
    pipe.incoming.assign(500, Ipv4(10, 0, 0, 1));
    pipe.next = 0;
    CHECK(Drain(filter).size() == 500 && filter.Dropped() == 2);

    // Without the extra range the VPN address is outside as well.
    ScriptedPipe plain;
    plain.incoming = { Ipv4(100, 64, 1, 2), Ipv4(10, 1, 2, 3) };
    sco::relay::AddressFilter strict(plain, {});
    CHECK(Drain(strict).size() == 1 && strict.Dropped() == 1);
}

}  // namespace

namespace relaytest {

void RunAddressRuleTests() {
    Run("TestBuiltInRangesMatchTheLibrary", TestBuiltInRangesMatchTheLibrary);
    Run("TestTheFilterLetsInTheRuleAndNothingElse", TestTheFilterLetsInTheRuleAndNothingElse);
}

}  // namespace relaytest

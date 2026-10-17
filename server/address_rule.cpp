// SPDX-License-Identifier: GPL-3.0-only
// The address rule of the server (server/address_rule.h): reading the extra ranges from the
// config file and describing the whole rule.
#include "server/address_rule.h"
#include "server/log.h"
#include <utility>

namespace sco::relay {

namespace {

// A range wider than /8 would let a large part of the internet in; players' own side refuses it too.
constexpr unsigned kWidestPrefixBits = 8;

// The library's private ranges, written out for the log. Keep in step with the library: the test
// TestBuiltInRangesMatchIsLan compares this text with IsPrivateAddress, and server/tools/check-net-boundary.sh
// pins the lines in sco-core that define them.
const char* const kBuiltInRanges = "127.0.0.0/8 10.0.0.0/8 172.16.0.0/12 192.168.0.0/16 169.254.0.0/16";

bool IsSeparator(char c) { return c == ',' || c == ' ' || c == '\t'; }

}  // namespace

bool ParseAllowedAddresses(std::string_view text, std::vector<AddressRange>* out, std::string* why) {
    std::vector<AddressRange> found;
    size_t pos = 0;
    while (pos < text.size()) {
        if (IsSeparator(text[pos])) {
            ++pos;
            continue;
        }
        size_t end = pos;
        while (end < text.size() && !IsSeparator(text[end])) ++end;
        const std::string entry(text.substr(pos, end - pos));
        pos = end;

        AddressRange range;
        if (!ParseAddressRange(entry, &range)) {
            *why = Format("'%s' is not a valid IPv4 range. Write a network address with its length, for example "
                          "100.64.0.0/10, or one address such as 203.0.113.7.", entry.c_str());
            return false;
        }
        if (range.family != Address::kIpv4) {
            *why = Format("'%s' is an IPv6 range, and the server listens on IPv4 only. Remove it.", entry.c_str());
            return false;
        }
        if (range.bits < kWidestPrefixBits) {
            *why = Format("'%s' is too wide. Use /%u or narrower, for example 100.64.0.0/10, so only the network "
                          "your players really use gets in.", entry.c_str(), kWidestPrefixBits);
            return false;
        }
        found.push_back(range);
    }
    if (found.empty()) {
        *why = "it lists no address range. Name at least one range, for example 100.64.0.0/10.";
        return false;
    }
    *out = std::move(found);
    return true;
}

std::string FormatCidr(const AddressRange& range) {
    return Format("%u.%u.%u.%u/%u", static_cast<unsigned>(range.bytes[0]), static_cast<unsigned>(range.bytes[1]),
                  static_cast<unsigned>(range.bytes[2]), static_cast<unsigned>(range.bytes[3]),
                  static_cast<unsigned>(range.bits));
}

std::string FormatSourceAddress(const Address& source) {
    if (source.family != Address::kIpv4) return "an unknown address";
    return Format("%u.%u.%u.%u", static_cast<unsigned>(source.bytes[0]), static_cast<unsigned>(source.bytes[1]),
                  static_cast<unsigned>(source.bytes[2]), static_cast<unsigned>(source.bytes[3]));
}

std::string DescribeAddressRule(const std::vector<AddressRange>& extra) {
    std::string text = kBuiltInRanges;
    for (const AddressRange& range : extra) {
        text += ' ';
        text += FormatCidr(range);
    }
    return text;
}

}  // namespace sco::relay

// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Which addresses may talk to the server. The rule is the one players already have: loopback and
// private ranges only, plus the IPv4 ranges listed in allowed_addresses, each /8 or narrower. There
// is no "allow every address" switch: a server on the internet lets in the ranges its players really
// come from, and nothing else. The rule itself is enforced by AddressFilter (server/net_boundary.h);
// this file reads the extra ranges from the config file and describes the whole rule for the log.
#include "server/net_boundary.h"
#include <string>
#include <string_view>
#include <vector>

namespace sco::relay {

// Reads "100.64.0.0/10, 203.0.113.7": entries separated by commas and/or spaces, an entry without
// "/n" being one address. false, with *why naming the entry and the fix, for anything else.
bool ParseAllowedAddresses(std::string_view text, std::vector<AddressRange>* out, std::string* why);

// "10.0.0.0/8", "203.0.113.7/32".
std::string FormatCidr(const AddressRange& range);

// "203.0.113.7" for an IPv4 source, without its port (a player's port changes with every start);
// "an unknown address" for anything else.
std::string FormatSourceAddress(const Address& source);

// The whole rule for the start-up log: the built-in private ranges, then the extra ones.
std::string DescribeAddressRule(const std::vector<AddressRange>& extra);

}  // namespace sco::relay

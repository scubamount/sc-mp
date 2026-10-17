// SPDX-License-Identifier: GPL-3.0-only
// Tests of the config reader, the address rule and the relay.info bytes: a refusal says the fix and
// names the line when one line is wrong, every setting is read, the example file stays in step with
// the reader.
#include "sco/net/core.h"
#include "server/address_rule.h"
#include "server/config.h"
#include "server/log.h"
#include "server/relay_info.h"
#include "plugins/multiplayer/relay_wire.h"
#include "server/tests/check.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {

using sco::relay::Address;
using sco::relay::AddressRange;
using sco::relay::Format;
using sco::relay::Settings;

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// The message ParseConfig gives for `text`, or "" when it accepts the text.
std::string Refusal(const std::string& text) {
    Settings settings;
    std::string why;
    return sco::relay::ParseConfig(text, &settings, &why) ? std::string() : why;
}

const char* const kRequired = "passphrase = a long passphrase\nmax_players = 15\n";

std::string ReadFile(const char* path) {
    std::string text;
    if (std::FILE* file = std::fopen(path, "rb")) {
        char chunk[4096];
        size_t got = 0;
        while ((got = std::fread(chunk, 1, sizeof chunk, file)) > 0) text.append(chunk, got);
        std::fclose(file);
    }
    return text;
}

void TestDefaults() {
    Settings s;
    std::string why;
    CHECK(sco::relay::ParseConfig(kRequired, &s, &why));
    CHECK(s.passphrase == "a long passphrase");
    CHECK(s.maxPlayers == 15);
    CHECK(s.port == sco::relay::kDefaultPort);
    CHECK(s.allowedAddresses.empty());
    // The timers start from the library's own defaults, not from a second copy of them.
    const sco::net::Options library;
    CHECK(s.joinTimeoutMs == library.joinTimeoutMs);
    CHECK(s.resendMs == library.resendMs && s.maxResendMs == library.maxResendMs);
    CHECK(s.keepaliveMs == library.keepaliveMs && s.peerTimeoutMs == library.peerTimeoutMs);
    CHECK(s.announceIntervalMs == 5000);
    // Nearby-only is off unless the file says on; its distance is above the plugin's 10 km show distance.
    CHECK(!s.nearbyOnly && s.nearbyDistanceM == 12000 && s.nearbyStaleMs == 2000);
    CHECK(s.nearbyDistanceM > sco::relay::wire::kPluginShowDistanceM);
}

void TestEverySetting() {
    const std::string text =
        "# a comment line\n"
        "port = 40000\n"
        "passphrase = pass # is not a comment\n"
        "max_players = 7\n"
        "server_name = My Relay\n"
        "allowed_addresses = 100.64.0.0/10, 203.0.113.7\n"
        "join_timeout_ms = 8000\n"
        "resend_ms = 150\n"
        "max_resend_ms = 1500\n"
        "keepalive_ms = 900\n"
        "peer_timeout_ms = 20000\n"
        "loop_wait_ms = 10\n"
        "announce_interval_ms = 2000\n"
        "stats_interval_ms = 5000\n"
        "log_level = debug\n"
        "nearby_only = on\n"
        "nearby_distance_m = 3000\n"
        "nearby_stale_ms = 800\n";
    Settings s;
    std::string why;
    CHECK(sco::relay::ParseConfig(text, &s, &why));
    CHECK(s.port == 40000);
    CHECK(s.passphrase == "pass # is not a comment");
    CHECK(s.maxPlayers == 7);
    CHECK(s.serverName == "My Relay");
    CHECK(s.allowedAddresses.size() == 2);
    CHECK(s.joinTimeoutMs == 8000 && s.resendMs == 150);
    CHECK(s.maxResendMs == 1500 && s.keepaliveMs == 900 && s.peerTimeoutMs == 20000);
    CHECK(s.loopWaitMs == 10 && s.announceIntervalMs == 2000 && s.statsIntervalMs == 5000);
    CHECK(s.logLevel == sco::relay::LogLevel::Debug);
    CHECK(s.nearbyOnly && s.nearbyDistanceM == 3000 && s.nearbyStaleMs == 800);
}

void TestNearbySettings() {
    // off is a value too, and the two numbers have a range each.
    Settings s;
    std::string why;
    CHECK(sco::relay::ParseConfig(std::string(kRequired) + "nearby_only = off\n", &s, &why) && !s.nearbyOnly);
    CHECK(Contains(Refusal(std::string(kRequired) + "nearby_only = yes\n"), "nearby_only must be on or off"));
    CHECK(Contains(Refusal(std::string(kRequired) + "nearby_only = yes\n"), "line 3"));
    CHECK(Contains(Refusal(std::string(kRequired) + "nearby_distance_m = 0\n"), "nearby_distance_m needs a whole number from 1 to"));
    CHECK(Contains(Refusal(std::string(kRequired) + "nearby_distance_m = 2000000000\n"), "nearby_distance_m needs"));
    CHECK(Refusal(std::string(kRequired) + "nearby_distance_m = 1\n").empty());
    // A stale time under 500 ms would call a player's position missing between two of its 100 ms reports.
    CHECK(Contains(Refusal(std::string(kRequired) + "nearby_stale_ms = 499\n"), "nearby_stale_ms needs a whole number from 500 to"));
    CHECK(Refusal(std::string(kRequired) + "nearby_stale_ms = 500\n").empty());

    // Settings built by hand are checked the same way as a file.
    Settings byHand;
    byHand.passphrase = "a long passphrase";
    byHand.maxPlayers = 3;
    byHand.nearbyStaleMs = 100;
    CHECK(!sco::relay::CheckSettings(byHand, &why) && Contains(why, "nearby_stale_ms"));
    byHand.nearbyStaleMs = 2000;
    byHand.nearbyDistanceM = 0;
    CHECK(!sco::relay::CheckSettings(byHand, &why) && Contains(why, "nearby_distance_m"));
}

void TestFileShape() {
    // Windows line endings, blank lines, tabs and spaces around names and values.
    Settings s;
    std::string why;
    CHECK(sco::relay::ParseConfig("\r\n  # note\r\n\tport\t=  5000 \r\npassphrase=a long passphrase\r\nmax_players = 3", &s, &why));
    CHECK(s.port == 5000 && s.passphrase == "a long passphrase" && s.maxPlayers == 3);
}

void TestByteOrderMark() {
    // Some Windows editors write a UTF-8 byte-order mark first. It is skipped, and line numbers do not move.
    const std::string mark = "\xEF\xBB\xBF";
    Settings s;
    std::string why;
    CHECK(sco::relay::ParseConfig(mark + kRequired, &s, &why));
    CHECK(s.maxPlayers == 15);
    CHECK(Contains(Refusal(mark + kRequired + "bogus = 1\n"), "line 3"));
}

void TestRefusals() {
    const std::string unknown = Refusal(std::string(kRequired) + "prot = 5\n");
    CHECK(Contains(unknown, "line 3") && Contains(unknown, "unknown setting 'prot'") && Contains(unknown, "port"));

    // The password stretching count is not a setting: a player cannot change it, so neither can the operator.
    CHECK(Contains(Refusal(std::string(kRequired) + "pbkdf2_iters = 1000\n"), "unknown setting 'pbkdf2_iters'"));

    CHECK(Contains(Refusal("max_players = 5\n"), "passphrase") && Contains(Refusal("max_players = 5\n"), "missing"));
    CHECK(Contains(Refusal("passphrase = short\nmax_players = 5\n"), "line 1"));
    CHECK(Contains(Refusal("passphrase = short\nmax_players = 5\n"), "at least 12"));
    CHECK(Contains(Refusal("passphrase = a long passphrase\n"), "max_players"));
    CHECK(Contains(Refusal(""), "passphrase"));

    const std::string tooMany = Refusal(Format("passphrase = a long passphrase\nmax_players = %u\n", sco::relay::kSessionSeats));
    CHECK(Contains(tooMany, "line 2"));
    CHECK(Contains(tooMany, Format("at most %u players", sco::relay::kSessionSeats - 1)));
    CHECK(Contains(tooMany, Format("Set max_players to %u or less", sco::relay::kSessionSeats - 1)));
    // In the shipped build the limit is 16 seats, so 16 players is refused with its reason, and 15 is not.
    if (sco::relay::kSessionSeats == 16) {
        const std::string sixteen = Refusal("passphrase = a long passphrase\nmax_players = 16\n");
        CHECK(Contains(sixteen, "line 2") && Contains(sixteen, "allows at most 15 players"));
        CHECK(Contains(sixteen, "a session holds 16 peers and the server is one of them"));
        CHECK(Refusal("passphrase = a long passphrase\nmax_players = 15\n").empty());
    }
    CHECK(Contains(Refusal("passphrase = a long passphrase\nmax_players = 0\n"), "max_players needs a whole number"));
    CHECK(Contains(Refusal("passphrase = a long passphrase\nmax_players = many\n"), "line 2"));

    // Every refusal of a port states the range that is accepted, 1024 to 65535, and no other.
    CHECK(Contains(Refusal(std::string(kRequired) + "port = http\n"), "port needs a whole number from 1024 to 65535"));
    CHECK(Contains(Refusal(std::string(kRequired) + "port = 0\n"), "port needs a whole number from 1024 to 65535"));
    CHECK(Contains(Refusal(std::string(kRequired) + "port = 70000\n"), "port needs a whole number from 1024 to 65535"));
    CHECK(!Contains(Refusal(std::string(kRequired) + "port = 0\n"), "1 to 65535"));

    // Players' games refuse to join a port below 1024, so such a server could not be reached.
    const std::string lowPort = Refusal(std::string(kRequired) + "port = 80\n");
    CHECK(Contains(lowPort, "line 3") && Contains(lowPort, "from 1024 to 65535"));
    CHECK(Contains(lowPort, "games cannot join a port below 1024"));
    CHECK(Refusal(std::string(kRequired) + "port = 1024\n").empty());

    // The players' side keeps 12 to 128 bytes of passphrase (the game's host control wants 12 at least).
    CHECK(Refusal(std::string("passphrase = ") + std::string(128, 'x') + "\nmax_players = 3\n").empty());
    CHECK(Refusal(std::string("passphrase = ") + std::string(12, 'x') + "\nmax_players = 3\n").empty());
    const std::string elevenBytes = Refusal(std::string("passphrase = ") + std::string(11, 'x') + "\nmax_players = 3\n");
    CHECK(Contains(elevenBytes, "line 1") && Contains(elevenBytes, "11 bytes long and needs at least 12"));
    const std::string longPass = Refusal(std::string("passphrase = ") + std::string(129, 'x') + "\nmax_players = 3\n");
    CHECK(Contains(longPass, "line 1") && Contains(longPass, "hold at most 128") && Contains(longPass, "Shorten it"));

    // The example file's passphrase is public, so the server will not start with it.
    const std::string placeholder = Refusal("max_players = 3\npassphrase = CHANGE-ME-to-a-long-secret-sentence\n");
    CHECK(Contains(placeholder, "line 2") && Contains(placeholder, "placeholder") && Contains(placeholder, "your own"));
    CHECK(Contains(Refusal(std::string(kRequired) + "join_timeout_ms = 0\n"), "join_timeout_ms needs a whole number"));
    // The library's retry timer is used only by a joining player, never by a host: it is not a setting.
    CHECK(Contains(Refusal(std::string(kRequired) + "retry_ms = 500\n"), "unknown setting 'retry_ms'"));
    CHECK(Contains(Refusal(std::string(kRequired) + "announce_interval_ms = 999\n"), "announce_interval_ms needs"));
    CHECK(Contains(Refusal(std::string(kRequired) + "stats_interval_ms = 999\n"), "stats_interval_ms needs"));

    CHECK(Contains(Refusal(std::string(kRequired) + "allowed_addresses = 10.0.0.0/7\n"), "too wide"));
    CHECK(Contains(Refusal(std::string(kRequired) + "allowed_addresses = 0.0.0.0/0\n"), "too wide"));
    CHECK(Contains(Refusal(std::string(kRequired) + "allowed_addresses = 10.1.2.3/8\n"), "not a valid IPv4 range"));
    CHECK(Contains(Refusal(std::string(kRequired) + "allowed_addresses = fd00::/8\n"), "IPv6"));
    CHECK(Contains(Refusal(std::string(kRequired) + "allowed_addresses = 100.64.0.0/10, nonsense\n"), "'nonsense'"));

    const std::string twice = Refusal(std::string(kRequired) + "port = 2000\nport = 3000\n");
    CHECK(Contains(twice, "line 4") && Contains(twice, "already set on line 3"));
    CHECK(Contains(Refusal(std::string(kRequired) + "port 5000\n"), "no '='"));
    CHECK(Contains(Refusal(std::string(kRequired) + "port =\n"), "has no value"));
    CHECK(Contains(Refusal(std::string(kRequired) + "log_level = loud\n"), "log_level must be one of"));
    CHECK(Contains(Refusal(std::string(kRequired) + "server_name = " + std::string(65, 'x') + "\n"), "server_name must be"));
    CHECK(Contains(Refusal(std::string("passphrase = a long\x01passphrase\nmax_players = 3\n")), "control character"));

    CHECK(Contains(Refusal(std::string(kRequired) + "resend_ms = 500\nmax_resend_ms = 100\n"), "max_resend_ms"));
    CHECK(Contains(Refusal(std::string(kRequired) + "keepalive_ms = 5000\npeer_timeout_ms = 5000\n"), "keepalive_ms"));

    // A player's game drops a server it has not heard from for the library's 30 s, and the server cannot
    // change that: a keepalive past a quarter of it is refused even when peer_timeout_ms would allow it. At a
    // quarter, pings at 1, 2 and 3 times keepalive_ms leave the third at three quarters of the timeout.
    const uint32_t mostKeepalive = sco::net::Options().peerTimeoutMs / 4;
    CHECK(mostKeepalive == 7500);
    const std::string slow = Refusal(Format("%skeepalive_ms = 40000\npeer_timeout_ms = 60000\n", kRequired));
    CHECK(Contains(slow, "keepalive_ms (40000) is too long"));
    CHECK(Contains(slow, Format("Set keepalive_ms to %u or less", mostKeepalive)));
    CHECK(Refusal(Format("%skeepalive_ms = %u\npeer_timeout_ms = 60000\n", kRequired, mostKeepalive)).empty());
    CHECK(!Refusal(Format("%skeepalive_ms = %u\npeer_timeout_ms = 60000\n", kRequired, mostKeepalive + 1)).empty());
    CHECK(Contains(Refusal(std::string(kRequired) + "loop_wait_ms = 300\n"), "loop_wait_ms"));
}

void TestLoadConfigFile() {
    Settings s;
    std::string why;
    CHECK(!sco::relay::LoadConfig("no-such-config-file.conf", &s, &why));
    CHECK(Contains(why, "cannot open") && Contains(why, "no-such-config-file.conf"));

    const char* path = "test-config-file.tmp";
    if (std::FILE* file = std::fopen(path, "wb")) {
        std::fputs("passphrase = a long passphrase\nmax_players = 4\nbogus = 1\n", file);
        std::fclose(file);
    }
    CHECK(!sco::relay::LoadConfig(path, &s, &why));
    CHECK(Contains(why, path) && Contains(why, "line 3"));
    std::remove(path);
}

void TestExampleConfig() {
    const std::string text = ReadFile(SCO_RELAY_EXAMPLE_CONFIG);
    CHECK(!text.empty());

    // As shipped, the example is refused for its public placeholder passphrase, and only for that.
    Settings s;
    std::string why;
    CHECK(!sco::relay::LoadConfig(SCO_RELAY_EXAMPLE_CONFIG, &s, &why));
    CHECK(Contains(why, "placeholder") && Contains(why, "line "));
    CHECK(Contains(text, std::string("passphrase = ") + sco::relay::kPlaceholderPassphrase));

    // With a passphrase of its own, every other line is accepted.
    std::string edited = text;
    const size_t at = edited.find(sco::relay::kPlaceholderPassphrase);
    if (at != std::string::npos) edited.replace(at, edited.find('\n', at) - at, "a long secret sentence of my own");
    CHECK(sco::relay::ParseConfig(edited, &s, &why));

    // The quick start copies the example unchanged, so as shipped it allows no range beyond the private
    // ones. The VPN range is only an example in a comment; without the # it is read like any other.
    CHECK(s.allowedAddresses.empty());
    CHECK(text.find("\nallowed_addresses = ") == std::string::npos);
    std::string withRange = edited;
    const size_t commented = withRange.find("# allowed_addresses = ");
    CHECK(commented != std::string::npos);
    if (commented != std::string::npos) withRange.erase(commented, 2);
    Settings withVpn;
    CHECK(sco::relay::ParseConfig(withRange, &withVpn, &why));
    CHECK(withVpn.allowedAddresses.size() == 1 && sco::relay::FormatCidr(withVpn.allowedAddresses[0]) == "100.64.0.0/10");

    // Every setting the reader knows is in the example, as a line of its own right under a comment
    // line. allowed_addresses is the one that stays commented out (see above).
    for (const std::string& name : sco::relay::SettingNames()) {
        const std::string prefix = name == "allowed_addresses" ? "# " : "";
        const size_t line = text.find("\n" + prefix + name + " = ");
        CHECK(line != std::string::npos);
        if (line == std::string::npos || line == 0) continue;
        const size_t previousStart = text.rfind('\n', line - 1);
        CHECK(text[previousStart == std::string::npos ? 0 : previousStart + 1] == '#');
    }
}

Address IPv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    Address address;
    address.family = Address::kIpv4;
    address.port = 5000;
    address.bytes[0] = a;
    address.bytes[1] = b;
    address.bytes[2] = c;
    address.bytes[3] = d;
    return address;
}

void TestAddressRule() {
    std::vector<AddressRange> extra;
    std::string why;
    CHECK(sco::relay::ParseAllowedAddresses("100.64.0.0/10 203.0.113.7,192.0.2.0/24", &extra, &why));
    CHECK(extra.size() == 3);
    CHECK(sco::relay::FormatCidr(extra[1]) == "203.0.113.7/32");

    const std::string described = sco::relay::DescribeAddressRule(extra);
    CHECK(Contains(described, "127.0.0.0/8") && Contains(described, "100.64.0.0/10") && Contains(described, "192.0.2.0/24"));

    // The extra ranges let the VPN in; the private ranges are the library's own; anything else stays out.
    // (The rule itself is enforced by AddressFilter, tested through the session in test_session.cpp.)
    const Address vpn = IPv4(100, 64, 1, 2), private_ = IPv4(192, 168, 1, 20), outside = IPv4(8, 8, 8, 8);
    CHECK(sco::relay::InAddressRange(extra[0], vpn) && !sco::relay::InAddressRange(extra[0], outside));
    CHECK(!sco::relay::IsPrivateAddress(vpn));
    CHECK(sco::relay::IsPrivateAddress(private_));
    CHECK(!sco::relay::IsPrivateAddress(outside));
}

void TestRelayInfoBytes() {
    uint8_t bytes[sco::relay::kRelayInfoBytes] = {};
    sco::relay::EncodeRelayInfo(sco::relay::RelayInfo{ 15 }, bytes);
    CHECK(bytes[0] == 1 && bytes[1] == 15 && bytes[2] == 0);

    sco::relay::EncodeRelayInfo(sco::relay::RelayInfo{ 0x0132 }, bytes);   // little-endian
    CHECK(bytes[1] == 0x32 && bytes[2] == 0x01);
    sco::relay::RelayInfo info;
    CHECK(sco::relay::DecodeRelayInfo(bytes, sizeof bytes, &info) && info.maxPlayers == 0x0132);

    CHECK(!sco::relay::DecodeRelayInfo(bytes, 2, &info));
    const uint8_t longer[4] = { 1, 15, 0, 0 };
    CHECK(!sco::relay::DecodeRelayInfo(longer, sizeof longer, &info));
    const uint8_t otherVersion[3] = { 2, 15, 0 };
    CHECK(!sco::relay::DecodeRelayInfo(otherVersion, sizeof otherVersion, &info));
    CHECK(!sco::relay::DecodeRelayInfo(nullptr, 3, &info));
}

}  // namespace

namespace relaytest {

void RunConfigTests() {
    Run("TestDefaults", TestDefaults);
    Run("TestEverySetting", TestEverySetting);
    Run("TestNearbySettings", TestNearbySettings);
    Run("TestFileShape", TestFileShape);
    Run("TestByteOrderMark", TestByteOrderMark);
    Run("TestRefusals", TestRefusals);
    Run("TestLoadConfigFile", TestLoadConfigFile);
    Run("TestExampleConfig", TestExampleConfig);
    Run("TestAddressRule", TestAddressRule);
    Run("TestRelayInfoBytes", TestRelayInfoBytes);
}

}  // namespace relaytest

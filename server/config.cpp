// SPDX-License-Identifier: GPL-3.0-only
// The settings of sco-relay and the reader of the config file they come from (server/config.h).
// One table lists the settings; each has a small function that reads its value or says what is wrong.
#include "server/config.h"
#include "server/address_rule.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace sco::relay {

namespace {

constexpr size_t   kMaxConfigBytes = 64 * 1024;   // a bigger file is not a sco-relay config file
constexpr uint32_t kMaxTimerMs = 3600000;         // an hour; a longer timer is a typo
constexpr uint32_t kHighestPort = 65535;

// A player's game drops a server it has not heard from for the library's own silence timer (30 s
// today). The plugin sets none of its own and the server cannot change it, so keepalive_ms must stay
// well below it. A quiet link is pinged after 1, 2 and 3 times keepalive_ms; the third ping has to
// arrive before the player gives up, with the loop wait and the transit on top, so that two pings in a
// row can be lost. At a third of the timer the third ping would be due at the very moment the player
// gives up, so the bound is a quarter of it: the third ping is due at three quarters.
constexpr uint32_t kPingsPerPlayerTimeout = 4;
uint32_t PlayerTimeoutMs() { return GetLibraryDefaults().peerTimeoutMs; }

// The stats line and the relay.info repeat: never more often than once a second, which would
// flood the log or the network; the stats line may be as rare as once a day.
constexpr uint32_t kMinReportIntervalMs = 1000;
constexpr uint32_t kMaxStatsIntervalMs = 86400000;

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// A whole number from min to max, nothing else (no sign, no spaces, no unit).
bool ParseNumber(const char* name, std::string_view text, uint32_t min, uint32_t max, uint32_t* out,
                 std::string* why) {
    uint64_t value = 0;
    bool digits = !text.empty() && text.size() <= 10;
    for (size_t i = 0; digits && i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') digits = false;
        else value = value * 10 + static_cast<uint64_t>(text[i] - '0');
    }
    if (!digits || value < min || value > max) {
        *why = Format("%s needs a whole number from %u to %u, but it is '%s'.", name, min, max,
                      std::string(text).c_str());
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool ParseTimerMs(const char* name, std::string_view text, uint32_t* out, std::string* why) {
    return ParseNumber(name, text, 1, kMaxTimerMs, out, why);
}

bool CheckPort(uint32_t port, std::string* why) {
    if (port >= kLowestPort) return true;
    *why = Format("port is %u, but players' games cannot join a port below %u, so nobody could connect. "
                  "Choose a port from %u to %u.", port, static_cast<unsigned>(kLowestPort),
                  static_cast<unsigned>(kLowestPort), kHighestPort);
    return false;
}

// The players' side counts bytes (a C string of 128 characters at most, and the host control of the
// game wants at least 12), so the server does too. A plain letter or digit is one byte.
bool CheckPassphrase(std::string_view passphrase, std::string* why) {
    if (passphrase.substr(0, std::strlen(kPlaceholderPassphrase)) == kPlaceholderPassphrase) {
        *why = Format("passphrase still starts with %s, the placeholder from the example file, and anyone can "
                      "read it there. Choose your own long secret and write it here.", kPlaceholderPassphrase);
        return false;
    }
    if (passphrase.size() < kMinPassphraseBytes) {
        *why = Format("passphrase is %zu bytes long and needs at least %u (a plain letter or digit is one byte). "
                      "Use a long one, a sentence is best: anyone who records the traffic can try to guess a "
                      "short one.", passphrase.size(), kMinPassphraseBytes);
        return false;
    }
    if (passphrase.size() > kMaxPassphraseBytes) {
        *why = Format("passphrase is %zu bytes long, but players' games hold at most %u, so nobody could type it. "
                      "Shorten it.", passphrase.size(), kMaxPassphraseBytes);
        return false;
    }
    return true;
}

// ---- one function per setting: read the value, put it in Settings, or say what is wrong ----------

bool SetPort(std::string_view value, Settings* s, std::string* why) {
    uint32_t number = 0;
    // One range, the one that is accepted, so no message names a wider one. Why 1024 is the lowest
    // is said only when the value is refused.
    if (!ParseNumber("port", value, kLowestPort, kHighestPort, &number, why)) {
        *why += Format(" Players' games cannot join a port below %u, so nobody could connect to one.",
                       static_cast<unsigned>(kLowestPort));
        return false;
    }
    s->port = static_cast<uint16_t>(number);
    return true;
}

bool SetPassphrase(std::string_view value, Settings* s, std::string* why) {
    if (!CheckPassphrase(value, why)) return false;
    s->passphrase = std::string(value);
    return true;
}

bool SetMaxPlayers(std::string_view value, Settings* s, std::string* why) {
    uint32_t number = 0;
    if (!ParseNumber("max_players", value, 1, std::numeric_limits<uint32_t>::max(), &number, why)) return false;
    if (!CheckPlayerLimit(number, why)) return false;
    s->maxPlayers = number;
    return true;
}

bool SetServerName(std::string_view value, Settings* s, std::string* why) {
    if (!IsValidPlayerName(value)) {
        *why = Format("server_name must be 1 to %u bytes with no control characters. Players see it as peer 1.",
                      kPlayerNameMaxBytes);
        return false;
    }
    s->serverName = std::string(value);
    return true;
}

bool SetAllowedAddresses(std::string_view value, Settings* s, std::string* why) {
    std::string problem;
    if (ParseAllowedAddresses(value, &s->allowedAddresses, &problem)) return true;
    *why = Format("allowed_addresses: %s", problem.c_str());
    return false;
}

bool SetAnnounceInterval(std::string_view value, Settings* s, std::string* why) {
    return ParseNumber("announce_interval_ms", value, kMinReportIntervalMs, kMaxTimerMs, &s->announceIntervalMs, why);
}

bool SetStatsInterval(std::string_view value, Settings* s, std::string* why) {
    return ParseNumber("stats_interval_ms", value, kMinReportIntervalMs, kMaxStatsIntervalMs, &s->statsIntervalMs,
                       why);
}

bool SetLogLevel(std::string_view value, Settings* s, std::string* why) {
    if (ParseLogLevel(value, &s->logLevel)) return true;
    *why = Format("log_level must be one of error, warn, info or debug, but it is '%s'.", std::string(value).c_str());
    return false;
}

bool SetNearbyOnly(std::string_view value, Settings* s, std::string* why) {
    if (value == "on" || value == "off") {
        s->nearbyOnly = value == "on";
        return true;
    }
    *why = Format("nearby_only must be on or off, but it is '%s'.", std::string(value).c_str());
    return false;
}

bool SetNearbyDistance(std::string_view value, Settings* s, std::string* why) {
    return ParseNumber("nearby_distance_m", value, 1, kMaxNearbyDistanceM, &s->nearbyDistanceM, why);
}

bool SetNearbyStale(std::string_view value, Settings* s, std::string* why) {
    return ParseNumber("nearby_stale_ms", value, kMinNearbyStaleMs, kMaxTimerMs, &s->nearbyStaleMs, why);
}

// ---- the table of settings -------------------------------------------------------------------------
// A setting is either read by its own function, or it is a plain timer: milliseconds from 1 to
// kMaxTimerMs stored in the Settings member named in the last column.

using ReadValue = bool (*)(std::string_view value, Settings* settings, std::string* why);

struct Key {
    const char*      name;
    ReadValue        read;
    uint32_t Settings::* timer;
};

const Key kKeys[] = {
    { "port",                 SetPort,             nullptr },
    { "passphrase",           SetPassphrase,       nullptr },
    { "max_players",          SetMaxPlayers,       nullptr },
    { "server_name",          SetServerName,       nullptr },
    { "allowed_addresses",    SetAllowedAddresses, nullptr },
    { "join_timeout_ms",      nullptr,             &Settings::joinTimeoutMs },
    { "resend_ms",            nullptr,             &Settings::resendMs },
    { "max_resend_ms",        nullptr,             &Settings::maxResendMs },
    { "keepalive_ms",         nullptr,             &Settings::keepaliveMs },
    { "peer_timeout_ms",      nullptr,             &Settings::peerTimeoutMs },
    { "loop_wait_ms",         nullptr,             &Settings::loopWaitMs },
    { "announce_interval_ms", SetAnnounceInterval, nullptr },
    { "stats_interval_ms",    SetStatsInterval,    nullptr },
    { "log_level",            SetLogLevel,         nullptr },
    { "nearby_only",          SetNearbyOnly,       nullptr },
    { "nearby_distance_m",    SetNearbyDistance,   nullptr },
    { "nearby_stale_ms",      SetNearbyStale,      nullptr },
};
constexpr size_t kKeyCount = sizeof kKeys / sizeof kKeys[0];

size_t FindKey(std::string_view name) {
    for (size_t i = 0; i < kKeyCount; ++i)
        if (name == kKeys[i].name) return i;
    return kKeyCount;
}

bool ReadKey(const Key& key, std::string_view value, Settings* settings, std::string* why) {
    if (key.timer) return ParseTimerMs(key.name, value, &(settings->*key.timer), why);
    return key.read(value, settings, why);
}

std::string ListKeys() {
    std::string list;
    for (const std::string& name : SettingNames()) {
        if (!list.empty()) list += ", ";
        list += name;
    }
    return list;
}

// ---- reading the text ------------------------------------------------------------------------------

// One line of the file. firstSeen[i] is the line where setting i was first given, or 0.
bool ReadLine(std::string_view line, uint32_t number, Settings* settings, std::vector<uint32_t>* firstSeen,
              std::string* why) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);   // a file edited on Windows
    for (char c : line) {
        const unsigned char u = static_cast<unsigned char>(c);
        if ((u < 0x20 && u != '\t') || u == 0x7F) {
            *why = "this line has a control character. The config file must be plain text.";
            return false;
        }
    }
    line = Trim(line);
    if (line.empty() || line.front() == '#') return true;

    const size_t equals = line.find('=');
    if (equals == std::string_view::npos) {
        *why = "expected 'name = value', but there is no '='. Fix the line, or start it with # to make it a comment.";
        return false;
    }
    const std::string_view name = Trim(line.substr(0, equals));
    const std::string_view value = Trim(line.substr(equals + 1));
    const size_t key = FindKey(name);
    if (key == kKeyCount) {
        *why = Format("unknown setting '%s'. The settings are: %s. Check the spelling, or remove the line.",
                      std::string(name).c_str(), ListKeys().c_str());
        return false;
    }
    if (value.empty()) {
        *why = Format("'%s' has no value. Write the value after the '=', or remove the line to keep the default.",
                      kKeys[key].name);
        return false;
    }
    if ((*firstSeen)[key] != 0) {
        *why = Format("'%s' is already set on line %u. Keep one of the two lines.", kKeys[key].name,
                      (*firstSeen)[key]);
        return false;
    }
    if (!ReadKey(kKeys[key], value, settings, why)) return false;
    (*firstSeen)[key] = number;
    return true;
}

}  // namespace

Settings::Settings() {
    const LibraryDefaults library = GetLibraryDefaults();   // the library's own defaults, so there is no second copy here
    joinTimeoutMs = library.joinTimeoutMs;
    resendMs = library.resendMs;
    maxResendMs = library.maxResendMs;
    keepaliveMs = library.keepaliveMs;
    peerTimeoutMs = library.peerTimeoutMs;
}

std::vector<std::string> SettingNames() {
    std::vector<std::string> names;
    for (const Key& key : kKeys) names.push_back(key.name);
    return names;
}

bool CheckPlayerLimit(uint32_t maxPlayers, std::string* why) {
    if (maxPlayers < kSessionSeats) return true;   // the server takes one of the places
    const uint32_t most = kSessionSeats - 1;
    *why = Format("max_players is %u, but the network library allows at most %u players: a session holds %u peers "
                  "and the server is one of them. Set max_players to %u or less.",
                  maxPlayers, most, kSessionSeats, most);
    return false;
}

bool CheckSettings(const Settings& s, std::string* why) {
    if (s.passphrase.empty()) {
        *why = "the setting 'passphrase' is missing. Add a line 'passphrase = ...' with a long secret you share "
               "with your players.";
        return false;
    }
    if (!CheckPassphrase(s.passphrase, why)) return false;
    if (s.maxPlayers == 0) {
        *why = "the setting 'max_players' is missing. Add a line 'max_players = N', where N is how many players "
               "the server should accept.";
        return false;
    }
    if (!CheckPlayerLimit(s.maxPlayers, why)) return false;
    if (!CheckPort(s.port, why)) return false;
    if (s.maxResendMs < s.resendMs) {
        *why = Format("max_resend_ms (%u) is smaller than resend_ms (%u). Raise max_resend_ms or lower resend_ms.",
                      s.maxResendMs, s.resendMs);
        return false;
    }
    if (s.keepaliveMs >= s.peerTimeoutMs) {
        *why = Format("keepalive_ms (%u) is not smaller than peer_timeout_ms (%u), so quiet players would be "
                      "dropped between two keepalives. Lower keepalive_ms or raise peer_timeout_ms.",
                      s.keepaliveMs, s.peerTimeoutMs);
        return false;
    }
    if (s.keepaliveMs > PlayerTimeoutMs() / kPingsPerPlayerTimeout) {
        *why = Format("keepalive_ms (%u) is too long. A player's game drops a server it has not heard from for %u ms, "
                      "and the server cannot change that, so a quiet player could time out between two pings. "
                      "Set keepalive_ms to %u or less, a quarter of that, so that two lost pings in a row do not "
                      "drop a player: the third still arrives well inside the timeout.",
                      s.keepaliveMs, PlayerTimeoutMs(), PlayerTimeoutMs() / kPingsPerPlayerTimeout);
        return false;
    }
    if (s.loopWaitMs > s.resendMs) {
        *why = Format("loop_wait_ms (%u) is larger than resend_ms (%u), so lost messages would be resent late. "
                      "Lower loop_wait_ms.", s.loopWaitMs, s.resendMs);
        return false;
    }
    if (s.announceIntervalMs < kMinReportIntervalMs || s.statsIntervalMs < kMinReportIntervalMs) {
        *why = Format("announce_interval_ms (%u) and stats_interval_ms (%u) must each be at least %u.",
                      s.announceIntervalMs, s.statsIntervalMs, kMinReportIntervalMs);
        return false;
    }
    if (s.nearbyDistanceM == 0 || s.nearbyDistanceM > kMaxNearbyDistanceM) {
        *why = Format("nearby_distance_m (%u) must be from 1 to %u metres.", s.nearbyDistanceM, kMaxNearbyDistanceM);
        return false;
    }
    if (s.nearbyStaleMs < kMinNearbyStaleMs) {
        *why = Format("nearby_stale_ms (%u) is too short: players send a position every 100 ms, and a position "
                      "older than this counts as missing. Use %u or more.", s.nearbyStaleMs, kMinNearbyStaleMs);
        return false;
    }
    return true;
}

bool ParseConfig(std::string_view text, Settings* out, std::string* why) {
    const std::string_view byteOrderMark = "\xEF\xBB\xBF";   // some Windows editors write it first
    if (text.substr(0, byteOrderMark.size()) == byteOrderMark) text.remove_prefix(byteOrderMark.size());

    Settings settings;
    std::vector<uint32_t> firstSeen(kKeyCount, 0);
    uint32_t number = 0;
    size_t start = 0;
    for (;;) {
        size_t end = text.find('\n', start);
        const bool lastLine = end == std::string_view::npos;
        if (lastLine) end = text.size();
        ++number;
        std::string problem;
        if (!ReadLine(text.substr(start, end - start), number, &settings, &firstSeen, &problem)) {
            *why = Format("line %u: %s", number, problem.c_str());
            return false;
        }
        if (lastLine) break;
        start = end + 1;
    }
    if (!CheckSettings(settings, why)) return false;
    *out = std::move(settings);
    return true;
}

bool LoadConfig(const std::string& path, Settings* out, std::string* why) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        *why = Format("cannot open the config file '%s' (%s). Check the path, or copy server/sco-relay.example.conf "
                      "and edit it.", path.c_str(), std::strerror(errno));
        return false;
    }
    std::string text;
    char chunk[4096];
    size_t got = 0;
    while (text.size() <= kMaxConfigBytes && (got = std::fread(chunk, 1, sizeof chunk, file)) > 0)
        text.append(chunk, got);
    const bool readFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (readFailed) {
        *why = Format("cannot read the config file '%s'. Check that it is a normal file you may read.", path.c_str());
        return false;
    }
    if (text.size() > kMaxConfigBytes) {
        *why = Format("'%s' is bigger than %zu bytes, so it is not a sco-relay config file. Check the path.",
                      path.c_str(), kMaxConfigBytes);
        return false;
    }
    std::string problem;
    if (!ParseConfig(text, out, &problem)) {
        *why = Format("%s: %s", path.c_str(), problem.c_str());
        return false;
    }
    return true;
}

}  // namespace sco::relay

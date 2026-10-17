// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The kit the session and run loop tests share: a world made of sco-core's in-memory network (a
// simulated clock, no sockets), a server transport and a log kept in a list, joiners (plain
// sco-core clients standing in for players), and a loop that pumps everyone. Nothing here sleeps.
#include "net_mem.h"
#include "sco/net/core.h"
#include "server/config.h"
#include "server/log.h"
#include "server/relay_info.h"
#include "server/session.h"
#include "server/tests/memory_end.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace relaytest {

// The protocol's 200000 stretching rounds are used once, in TestDefaultIterations; the other tests
// pass this smaller count to RelaySession and to their joiners to stay fast.
inline constexpr uint32_t kTestIters = 1000;
inline constexpr const char* kPassphrase = "test passphrase";

inline bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

inline sco::relay::Settings TestSettings(uint32_t maxPlayers) {
    sco::relay::Settings s;
    s.passphrase = kPassphrase;
    s.maxPlayers = maxPlayers;
    s.serverName = "Relay";
    s.statsIntervalMs = 1000;
    return s;
}

inline sco::net::Options JoinOptions(const char* name, const char* passphrase, uint32_t iters, uint64_t seed) {
    sco::net::Options o;
    o.playerName = name;
    o.passphrase = passphrase;
    o.pbkdf2Iters = iters;
    auto rng = std::make_shared<std::mt19937_64>(seed);
    o.random = [rng](uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>((*rng)());
    };
    return o;
}

struct Received {
    std::string channel;
    sco::net::PeerId from = 0;
    std::vector<uint8_t> data;
};

// A player: a sco-core Core joining the server over the in-memory network.
struct Joiner {
    nettest::MemTransport transport;
    sco::net::Core core;
    std::vector<Received> got;           // every message delivered to this player
    std::vector<std::string> events;     // "J2:Ann" (peer 2 joined), "L3:left: bye" (peer 3 left)
    int downs = 0;                       // times the session ended or the join failed
    std::string lastDown;                // and why
    uint16_t id;

    Joiner(nettest::MemNet& net, uint16_t endpointId) : transport(net, endpointId), core(transport, MakeCallbacks()), id(endpointId) {}

    sco::net::Callbacks MakeCallbacks() {
        sco::net::Callbacks cb;
        cb.message = [this](const sco::net::Message& m) {
            got.push_back(Received{ std::string(m.channel), m.from, std::vector<uint8_t>(m.data, m.data + m.len) });
        };
        cb.peer = [this](sco::net::PeerEvent e, sco::net::PeerId peer, std::string_view text) {
            std::string event = e == sco::net::PeerEvent::Joined ? "J" : "L";
            event += std::to_string(peer);
            event += ':';
            event.append(text);
            events.push_back(event);
        };
        cb.state = [this](bool active, std::string_view reason) {
            if (active) return;
            ++downs;
            lastDown = std::string(reason);
        };
        return cb;
    }

    // Like the real client: channels are registered before joining.
    void RegisterInfo() {
        core.RegisterChannel(sco::relay::kRelayInfoChannel, sco::net::kReliable | sco::net::kFromHost,
                             sco::relay::kRelayInfoBytes);
    }
    void RegisterGame() {
        core.RegisterChannel("game.pose", 0, sco::net::kMaxUnreliable);
        core.RegisterChannel("game.spawn", sco::net::kReliable, 4096);
    }

    bool Join(uint64_t now, const char* name, const char* passphrase = kPassphrase, uint32_t iters = kTestIters) {
        return core.Join(JoinOptions(name, passphrase, iters, uint64_t{ id } * 7919), nettest::MemEndpoint(1), now);
    }
    bool Up() const { return core.Active() && core.GetState() == sco::net::State::Joined; }
    size_t Count(const std::string& channel) const {
        size_t n = 0;
        for (const Received& r : got)
            if (r.channel == channel) ++n;
        return n;
    }
    bool HasEvent(const std::string& event) const {
        for (const std::string& e : events)
            if (e == event) return true;
        return false;
    }
};

// The network, the server's end of it (endpoint 1) and the server's log.
struct World {
    nettest::MemNet net{ 1 };
    nettest::MemTransport serverMemory{ net, 1 };
    MemoryEnd serverTransport{ serverMemory };   // what the session is given
    std::vector<std::string> lines;
    std::vector<sco::relay::LogLevel> levels;   // the level of each line, in the same order
    sco::relay::Logger log{ sco::relay::LogLevel::Debug, [this](sco::relay::LogLevel level, const std::string& line) {
                               levels.push_back(level);
                               lines.push_back(line);
                           } };

    bool Logged(const std::string& part) const {
        for (const std::string& line : lines)
            if (Contains(line, part)) return true;
        return false;
    }
    // How many lines at `level` hold `part`.
    size_t CountAt(sco::relay::LogLevel level, const std::string& part) const {
        size_t n = 0;
        for (size_t i = 0; i < lines.size(); ++i)
            if (levels[i] == level && Contains(lines[i], part)) ++n;
        return n;
    }
};

using Joiners = std::vector<Joiner*>;

// Pumps the server and every joiner, 5 ms of simulated time at a time, until done() or forMs passed.
inline bool RunUntil(World& w, sco::relay::RelaySession& session, const Joiners& joiners, uint64_t forMs,
                     const std::function<bool()>& done) {
    const uint64_t until = w.net.now + forMs;
    for (;;) {
        session.Pump(w.net.now);
        for (Joiner* j : joiners) j->core.Pump(w.net.now);
        if (done()) return true;
        if (w.net.now >= until) return false;
        w.net.now += 5;
    }
}

// Starts `joiner` joining and runs until it is in.
inline bool JoinNow(World& w, sco::relay::RelaySession& session, const Joiners& all, Joiner& joiner, const char* name) {
    if (!joiner.Join(w.net.now, name)) return false;
    return RunUntil(w, session, all, 5000, [&joiner] { return joiner.Up(); });
}

}  // namespace relaytest
